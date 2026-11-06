#include <boost/test/unit_test.hpp>

#include <boost/json.hpp>
#include <chrono>
#include <limits>
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
    id.build_id = "novaTest";
    id.interval_ms = 250;
    return id;
}

json::object parse(const std::string& text) {
    return json::parse(text).as_object();
}

double number(const json::value& v) {
    return v.is_double() ? v.as_double() : static_cast<double>(v.to_number<std::int64_t>());
}

std::string text(const json::value& v) {
    return std::string(v.as_string().c_str());
}

// Percentiles come from log-spaced buckets: within this fraction of the truth.
const auto kHistogramTolerance = boost::test_tools::tolerance(0.045);
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

BOOST_AUTO_TEST_CASE(the_interval_is_a_whole_number_of_milliseconds) {
    int ms = 1000;
    BOOST_TEST(parse_stats_interval("250", ms));
    BOOST_TEST(ms == 250);
    BOOST_TEST(parse_stats_interval("1", ms));
    BOOST_TEST(ms == 1);
    BOOST_TEST(parse_stats_interval("60000", ms));
    BOOST_TEST(ms == 60000);
}

BOOST_AUTO_TEST_CASE(an_unset_interval_keeps_the_default) {
    int ms = 1000;
    BOOST_TEST(parse_stats_interval(nullptr, ms));
    BOOST_TEST(parse_stats_interval("", ms));
    BOOST_TEST(ms == 1000);
}

BOOST_AUTO_TEST_CASE(a_malformed_interval_is_reported_and_keeps_the_default) {
    for (const char* v : {"0", "-5", "1.5", "abc", "10ms", " 5", "5 ", "+5", "0x10", "99999999999"}) {
        int ms = 1000;
        BOOST_TEST_CONTEXT(v) {
            BOOST_TEST(!parse_stats_interval(v, ms));
            BOOST_TEST(ms == 1000);
        }
    }
}

BOOST_AUTO_TEST_CASE(histogram_percentiles_are_close_and_min_max_are_exact) {
    Histogram h;
    for (int i = 100; i >= 1; --i) h.add(i);  // unsorted on purpose
    Summary s = h.summarize();
    BOOST_TEST(s.count == 100u);
    BOOST_TEST(s.min == 1.0);
    BOOST_TEST(s.max == 100.0);
    BOOST_TEST(s.p50 == 50.0, kHistogramTolerance);
    BOOST_TEST(s.p99 == 99.0, kHistogramTolerance);
    BOOST_TEST(s.p50 >= 50.0);  // read from the top of a bucket, so never low
    BOOST_TEST(s.p99 >= 99.0);
}

BOOST_AUTO_TEST_CASE(histogram_of_one_value_and_of_none) {
    Histogram h;
    BOOST_TEST(h.summarize().count == 0u);
    BOOST_TEST(h.summarize().max == 0.0);
    h.add(7.5);
    Summary one = h.summarize();
    BOOST_TEST(one.count == 1u);
    // Clamped to what was seen, so a lone value is reported exactly.
    BOOST_TEST(one.min == 7.5);
    BOOST_TEST(one.p50 == 7.5);
    BOOST_TEST(one.p99 == 7.5);
    BOOST_TEST(one.max == 7.5);
}

BOOST_AUTO_TEST_CASE(histogram_ignores_nan_and_negative_values_and_clamps_the_ends) {
    Histogram h;
    h.add(std::numeric_limits<double>::quiet_NaN());
    h.add(-1.0);
    h.add(std::numeric_limits<double>::infinity());
    BOOST_TEST(h.summarize().count == 0u);

    h.add(0.0);       // below the lowest bucket
    h.add(1.0e9);     // beyond the highest
    Summary s = h.summarize();
    BOOST_TEST(s.count == 2u);
    BOOST_TEST(s.min == 0.0);
    BOOST_TEST(s.max == 1.0e9);  // still exact
}

BOOST_AUTO_TEST_CASE(histogram_clear_forgets_everything) {
    Histogram h;
    h.add(3.0);
    h.clear();
    BOOST_TEST(h.summarize().count == 0u);
    h.add(9.0);
    BOOST_TEST(h.summarize().min == 9.0);
}

BOOST_AUTO_TEST_CASE(a_slow_loop_keeps_meaningful_percentiles_in_the_total) {
    // 10 values a window: the window's p99 is just its max, but the total
    // accumulates 100 values across ten windows.
    Distribution d;
    Distribution::Report r;
    for (int w = 0; w < 10; ++w) {
        for (int i = 1; i <= 10; ++i) d.add(i * 10.0);
        r = d.take();
    }
    BOOST_TEST(r.last.count == 10u);
    BOOST_TEST(r.total.count == 100u);
    BOOST_TEST(r.total.p50 == 50.0, kHistogramTolerance);
    BOOST_TEST(r.total.max == 100.0);
}

BOOST_AUTO_TEST_CASE(a_distribution_starts_a_new_last_window_but_keeps_the_total) {
    Distribution d;
    d.add(1.0);
    d.add(2.0);
    auto first = d.take();
    BOOST_TEST(first.last.count == 2u);
    d.add(3.0);
    auto second = d.take();
    BOOST_TEST(second.last.count == 1u);
    BOOST_TEST(second.last.min == 3.0);
    BOOST_TEST(second.total.count == 3u);
    BOOST_TEST(second.total.min == 1.0);
    BOOST_TEST(d.take().last.count == 0u);
}

BOOST_AUTO_TEST_CASE(loop_periods_are_measured_between_ticks) {
    LoopStats loop(10.0);
    auto t = steady_clock::now();
    loop.tick(t);  // starts the clock; no period yet
    loop.tick(t += milliseconds(10));
    loop.tick(t += milliseconds(12));
    loop.tick(t += milliseconds(30));
    auto snap = loop.take();
    BOOST_TEST(snap.configured_ms == 10.0);
    BOOST_TEST(snap.ticks == 4u);
    BOOST_TEST(snap.period_ms.last.count == 3u);
    BOOST_TEST(snap.period_ms.last.min == 10.0, boost::test_tools::tolerance(1e-6));
    BOOST_TEST(snap.period_ms.last.max == 30.0, boost::test_tools::tolerance(1e-6));
}

BOOST_AUTO_TEST_CASE(an_overrun_is_a_pass_that_lost_a_whole_tick) {
    LoopStats loop(10.0);
    auto t = steady_clock::now();
    loop.tick(t);
    loop.tick(t += milliseconds(15));  // late, but not a whole interval
    loop.tick(t += milliseconds(19));  // just under two intervals
    BOOST_TEST(loop.take().overruns == 0u);
    loop.tick(t += milliseconds(20));  // exactly two intervals: a tick lost
    loop.tick(t += milliseconds(35));
    BOOST_TEST(loop.take().overruns == 2u);
}

BOOST_AUTO_TEST_CASE(loop_windows_reset_but_counters_and_totals_keep_counting) {
    LoopStats loop(10.0);
    auto t = steady_clock::now();
    loop.tick(t);
    loop.tick(t += milliseconds(40));
    BOOST_TEST(loop.take().overruns == 1u);
    loop.tick(t += milliseconds(10));
    auto snap = loop.take();
    BOOST_TEST(snap.period_ms.last.count == 1u);   // only since the last take
    BOOST_TEST(snap.period_ms.total.count == 2u);  // since the start
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

BOOST_AUTO_TEST_CASE(the_stats_message_is_measurements_only) {
    StatsRegistry stats(identity(), true);
    auto* sampler = stats.loop("sampler", 1);
    BOOST_REQUIRE(sampler != nullptr);
    BOOST_TEST(stats.loop("sampler", 99) == sampler);  // same one; the first period wins
    BOOST_TEST(sampler->configured_ms() == 1.0);
    auto t = steady_clock::now();
    sampler->tick(t);
    sampler->tick(t + milliseconds(1));
    stats.record_command("relay", 0.1, 0.5);
    stats.record_command("relay", 0.2, 0.9);
    stats.count_publish_failure();
    stats.count_daq_read_errors(2);

    auto msg = parse(stats.take_stats_json());

    // The node is the topic and the rest is in the info message, so none of it
    // is repeated here.
    for (const char* key : {"node", "env", "build_id", "git_commit", "actuation"}) {
        BOOST_TEST_CONTEXT(key) { BOOST_TEST(!msg.contains(key)); }
    }
    BOOST_TEST(number(msg.at("seq")) == 1.0);
    BOOST_TEST(number(msg.at("time_ms")) > 1.0e12);  // a wall-clock time in ms
    BOOST_TEST(number(msg.at("uptime_s")) >= 0.0);

    const auto& loop = msg.at("loops").at("sampler").as_object();
    BOOST_TEST(!loop.contains("configured_ms"));  // constant: it is in the info message
    BOOST_TEST(number(loop.at("ticks")) == 2.0);
    BOOST_TEST(number(loop.at("overruns")) == 0.0);
    BOOST_TEST(number(loop.at("period_ms").at("last").at("count")) == 1.0);
    BOOST_TEST(number(loop.at("period_ms").at("total").at("count")) == 1.0);

    const auto& relay = msg.at("commands").at("relay").as_object();
    BOOST_TEST(number(relay.at("count")) == 2.0);
    BOOST_TEST(number(relay.at("received_to_handled_ms").at("last").at("max")) == 0.2);
    BOOST_TEST(number(relay.at("received_to_done_ms").at("total").at("max")) == 0.9);

    BOOST_TEST(number(msg.at("publish_failures")) == 1.0);
    BOOST_TEST(number(msg.at("daq_read_errors")) == 2.0);

    // The next message has the next number, an emptied `last` and an intact
    // `total` and count.
    auto next = parse(stats.take_stats_json());
    BOOST_TEST(number(next.at("seq")) == 2.0);
    const auto& relay2 = next.at("commands").at("relay").as_object();
    BOOST_TEST(number(relay2.at("count")) == 2.0);
    BOOST_TEST(number(relay2.at("received_to_done_ms").at("last").at("count")) == 0.0);
    BOOST_TEST(number(relay2.at("received_to_done_ms").at("total").at("count")) == 2.0);
}

BOOST_AUTO_TEST_CASE(the_info_message_has_identity_loops_and_how_to_read_the_stats) {
    StatsRegistry stats(identity(), true);
    stats.loop("publisher", 250);
    stats.loop("sampler", 1);
    auto info = parse(stats.identity_json());

    BOOST_TEST(!info.contains("node"));  // the topic names it
    // Nothing about the settings the program runs with, for now.
    for (const char* key : {"env", "git_commit", "actuation"}) {
        BOOST_TEST_CONTEXT(key) { BOOST_TEST(!info.contains(key)); }
    }
    BOOST_TEST(number(info.at("schema")) == 1.0);
    BOOST_TEST(text(info.at("build_id")) == "novaTest");
    BOOST_TEST(number(info.at("stats_interval_ms")) == 250.0);
    BOOST_TEST(number(info.at("started_ms")) > 1.0e12);

    const auto& loops = info.at("loops").as_object();
    BOOST_TEST(loops.size() == 2u);
    BOOST_TEST(number(loops.at("publisher").at("configured_ms")) == 250.0);
    BOOST_TEST(number(loops.at("sampler").at("configured_ms")) == 1.0);

    const auto& how = info.at("measurement").as_object();
    BOOST_TEST(number(how.at("overrun_period_factor")) == LoopStats::kOverrunFactor);
    BOOST_TEST(number(how.at("buckets_per_octave")) == Histogram::kBucketsPerOctave);
    BOOST_TEST(number(how.at("relative_error")) == Histogram::kRelativeError);

    // It describes the process, not the second: asking again changes nothing.
    BOOST_TEST(stats.identity_json() == stats.identity_json());
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
    double handled = number(relay.at("received_to_handled_ms").at("total").at("max"));
    double done = number(relay.at("received_to_done_ms").at("total").at("max"));
    BOOST_TEST(handled >= 20.0);
    BOOST_TEST(done >= handled);  // the handler's 5 ms is inside `done` only
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
