#include <boost/test/unit_test.hpp>

#include <boost/json.hpp>
#include <chrono>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "controllers/sampler.hpp"
#include "core/command_router.hpp"
#include "core/stats.hpp"

namespace json = boost::json;
using namespace std::chrono;

namespace {
StatsIdentity identity() {
    StatsIdentity id;
    id.node = "novaTest";
    id.env = "dev";
    id.build_id = "novaTest";
    id.git_commit = "abc1234";
    return id;
}

json::object parse(const std::string& text) {
    return json::parse(text).as_object();
}

double number(const json::value& v) {
    return v.is_double() ? v.as_double() : static_cast<double>(v.to_number<std::int64_t>());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(stats)

BOOST_AUTO_TEST_CASE(the_setting_reads_on_and_off_spellings) {
    bool on = false;
    for (const char* v : {"1", "true", "TRUE", "On", "yes"}) {
        BOOST_TEST_CONTEXT(v) {
            BOOST_TEST(parse_stats_setting(v, on));
            BOOST_TEST(on);
        }
    }
    for (const char* v : {"0", "false", "off", "No", ""}) {
        on = true;
        BOOST_TEST_CONTEXT(v) {
            BOOST_TEST(parse_stats_setting(v, on));
            BOOST_TEST(!on);
        }
    }
    on = true;
    BOOST_TEST(parse_stats_setting(nullptr, on));  // unset is off
    BOOST_TEST(!on);
}

BOOST_AUTO_TEST_CASE(a_malformed_setting_is_reported_and_leaves_stats_off) {
    bool on = true;
    BOOST_TEST(!parse_stats_setting("maybe", on));
    BOOST_TEST(!on);
    BOOST_TEST(!parse_stats_setting("2", on));
    BOOST_TEST(!on);
}

BOOST_AUTO_TEST_CASE(summary_uses_nearest_rank_percentiles) {
    std::vector<double> v;
    for (int i = 100; i >= 1; --i) v.push_back(i);  // unsorted on purpose
    Summary s = summarize(v);
    BOOST_TEST(s.count == 100u);
    BOOST_TEST(s.min == 1.0);
    BOOST_TEST(s.p50 == 50.0);
    BOOST_TEST(s.p99 == 99.0);
    BOOST_TEST(s.max == 100.0);
}

BOOST_AUTO_TEST_CASE(summary_of_one_value_and_of_none) {
    Summary one = summarize({7.5});
    BOOST_TEST(one.count == 1u);
    BOOST_TEST(one.min == 7.5);
    BOOST_TEST(one.p50 == 7.5);
    BOOST_TEST(one.p99 == 7.5);
    BOOST_TEST(one.max == 7.5);
    Summary none = summarize({});
    BOOST_TEST(none.count == 0u);
    BOOST_TEST(none.max == 0.0);
}

BOOST_AUTO_TEST_CASE(a_window_empties_when_taken_and_caps_what_it_stores) {
    Window w;
    for (std::size_t i = 0; i < Window::kMaxWindow + 5; ++i) w.add(1.0);
    Summary s = w.take();
    BOOST_TEST(s.count == Window::kMaxWindow + 5);  // all counted
    BOOST_TEST(w.take().count == 0u);
}

BOOST_AUTO_TEST_CASE(loop_periods_are_measured_between_ticks) {
    LoopStats loop(10.0);
    auto t = steady_clock::now();
    loop.tick(t);  // starts the clock; no period yet
    loop.tick(t += milliseconds(10));
    loop.tick(t += milliseconds(12));
    loop.tick(t += milliseconds(30));  // a 3x overrun
    auto snap = loop.take();
    BOOST_TEST(snap.configured_ms == 10.0);
    BOOST_TEST(snap.ticks == 4u);
    BOOST_TEST(snap.period_ms.count == 3u);
    BOOST_TEST(snap.period_ms.min == 10.0, boost::test_tools::tolerance(1e-6));
    BOOST_TEST(snap.period_ms.max == 30.0, boost::test_tools::tolerance(1e-6));
    BOOST_TEST(snap.overruns == 1u);  // 12 ms is under 1.5x; 30 ms is not
}

BOOST_AUTO_TEST_CASE(period_windows_reset_but_counters_keep_counting) {
    LoopStats loop(10.0);
    auto t = steady_clock::now();
    loop.tick(t);
    loop.tick(t += milliseconds(40));
    BOOST_TEST(loop.take().overruns == 1u);
    loop.tick(t += milliseconds(10));
    auto snap = loop.take();
    BOOST_TEST(snap.period_ms.count == 1u);  // only the period since the last take
    BOOST_TEST(snap.overruns == 1u);
    BOOST_TEST(snap.ticks == 3u);
}

BOOST_AUTO_TEST_CASE(a_disabled_registry_records_and_hands_out_nothing) {
    StatsRegistry stats(identity(), false);
    BOOST_TEST(!stats.enabled());
    BOOST_TEST(stats.loop("sampler", 1) == nullptr);
    stats.record_command("relay", 1.0, 2.0);
    stats.count_publish_failure();
    stats.count_daq_read_errors(3);
    auto msg = parse(stats.take_stats_json());
    BOOST_TEST(msg.at("commands").as_object().empty());
    BOOST_TEST(msg.at("loops").as_object().empty());
    BOOST_TEST(number(msg.at("publish_failures")) == 0.0);
    BOOST_TEST(number(msg.at("daq_read_errors")) == 0.0);
}

BOOST_AUTO_TEST_CASE(telemetry_seq_counts_up_from_one_across_threads) {
    StatsRegistry stats(identity(), true);
    BOOST_TEST(stats.next_telemetry_seq() == 1u);
    BOOST_TEST(stats.next_telemetry_seq() == 2u);

    // Meaningful under ThreadSanitizer: no value is handed out twice.
    std::vector<std::vector<std::uint64_t>> got(4);
    std::vector<std::thread> threads;
    for (auto& g : got) {
        threads.emplace_back([&stats, &g] {
            for (int i = 0; i < 500; ++i) g.push_back(stats.next_telemetry_seq());
        });
    }
    for (auto& t : threads) t.join();
    std::set<std::uint64_t> all;
    for (auto& g : got) all.insert(g.begin(), g.end());
    BOOST_TEST(all.size() == 2000u);
    BOOST_TEST(*all.begin() == 3u);
    BOOST_TEST(*all.rbegin() == 2002u);
}

BOOST_AUTO_TEST_CASE(the_stats_message_has_identity_loops_commands_and_counters) {
    StatsRegistry stats(identity(), true);
    auto* sampler = stats.loop("sampler", 1);
    BOOST_REQUIRE(sampler != nullptr);
    BOOST_TEST(stats.loop("sampler", 1) == sampler);  // same one every time
    sampler->tick();
    stats.record_command("relay", 0.1, 0.5);
    stats.record_command("relay", 0.2, 0.9);
    stats.count_publish_failure();
    stats.count_daq_read_errors(2);

    auto msg = parse(stats.take_stats_json());
    BOOST_TEST(std::string(msg.at("node").as_string().c_str()) == "novaTest");
    BOOST_TEST(std::string(msg.at("env").as_string().c_str()) == "dev");
    BOOST_TEST(std::string(msg.at("build_id").as_string().c_str()) == "novaTest");
    BOOST_TEST(std::string(msg.at("git_commit").as_string().c_str()) == "abc1234");
    BOOST_TEST(std::string(msg.at("actuation").as_string().c_str()) == "enabled");
    BOOST_TEST(number(msg.at("seq")) == 1.0);
    BOOST_TEST(number(msg.at("time_ms")) > 1.0e12);  // a wall-clock time in ms

    const auto& loop = msg.at("loops").at("sampler").as_object();
    BOOST_TEST(number(loop.at("configured_ms")) == 1.0);
    BOOST_TEST(number(loop.at("ticks")) == 1.0);
    BOOST_TEST(number(loop.at("overruns")) == 0.0);

    const auto& relay = msg.at("commands").at("relay").as_object();
    BOOST_TEST(number(relay.at("count")) == 2.0);
    BOOST_TEST(number(relay.at("received_to_handled_ms").at("max")) == 0.2);
    BOOST_TEST(number(relay.at("received_to_done_ms").at("max")) == 0.9);

    BOOST_TEST(number(msg.at("publish_failures")) == 1.0);
    BOOST_TEST(number(msg.at("daq_read_errors")) == 2.0);

    // Each message gets the next number, and the per-message windows start over
    // while the counts carry on.
    auto next = parse(stats.take_stats_json());
    BOOST_TEST(number(next.at("seq")) == 2.0);
    const auto& relay2 = next.at("commands").at("relay").as_object();
    BOOST_TEST(number(relay2.at("count")) == 2.0);
    BOOST_TEST(number(relay2.at("received_to_done_ms").at("count")) == 0.0);
}

BOOST_AUTO_TEST_CASE(the_router_times_commands_by_type) {
    StatsRegistry stats(identity(), true);
    CommandRouter router("novaOps");
    router.register_handler("relay", [](const json::object&) {
        std::this_thread::sleep_for(milliseconds(5));
    });
    router.register_handler("boom", [](const json::object&) { throw std::runtime_error("x"); });
    router.set_stats(&stats);

    // Received 20 ms ago, as if it had waited in the queue.
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":1}})",
                          steady_clock::now() - milliseconds(20));
    router.handle_message(R"({"source":"novaOps","command":{"type":"boom"}})");
    router.handle_message(R"({"source":"someoneElse","command":{"type":"relay"}})");  // not ours
    router.handle_message(R"({"source":"novaOps","command":{"type":"warp"}})");       // no handler
    router.handle_message("not json");

    auto msg = parse(stats.take_stats_json());
    const auto& commands = msg.at("commands").as_object();
    BOOST_TEST(commands.size() == 2u);  // relay and boom only
    const auto& relay = commands.at("relay").as_object();
    BOOST_TEST(number(relay.at("count")) == 1.0);
    BOOST_TEST(number(relay.at("received_to_handled_ms").at("max")) >= 20.0);
    BOOST_TEST(number(relay.at("received_to_done_ms").at("max")) >=
               number(relay.at("received_to_handled_ms").at("max")) + 5.0);
    // A handler that throws still counts: the command arrived.
    BOOST_TEST(number(commands.at("boom").at("count")) == 1.0);
}

BOOST_AUTO_TEST_CASE(failed_daq_reads_are_counted) {
    class FailingDaq : public AnalogIo {
    public:
        std::vector<DaqHatDevice> discover() override { return {}; }
        bool open(const DaqHatDevice&) override { return true; }
        bool read(const DaqHatDevice&, int channel, double& value) override {
            value = 1.0;
            return channel != 1;  // channel 1 always fails
        }
    };
    DaqHatDevice hat;
    hat.address = 0;
    hat.channels = {0, 1, 2};
    FailingDaq daq;
    TelemetryStore store;
    StatsRegistry stats(identity(), true);
    daq_sample_once(daq, {hat}, store, nullptr, &stats);
    daq_sample_once(daq, {hat}, store, nullptr, &stats);
    auto msg = parse(stats.take_stats_json());
    BOOST_TEST(number(msg.at("daq_read_errors")) == 2.0);
}

BOOST_AUTO_TEST_SUITE_END()
