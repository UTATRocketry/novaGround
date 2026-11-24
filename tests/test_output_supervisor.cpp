#include <boost/test/unit_test.hpp>

#include <bitset>
#include <boost/json.hpp>
#include <chrono>
#include <string>
#include <vector>

#include "controllers/gpio_controller.hpp"
#include "controllers/relay_controller.hpp"
#include "core/actuation_guard.hpp"
#include "core/output_supervisor.hpp"
#include "core/stats.hpp"
#include "core/telemetry.hpp"
#include "fake_digital_io.hpp"

namespace json = boost::json;
using namespace std::chrono;

namespace {
class FakeRelayBank : public RelayBank {
public:
    std::vector<std::bitset<16>> writes;
    std::bitset<16> held{0xFFFF};
    bool fail = false;

    bool is_ready() const override { return true; }
    bool write_output(const std::bitset<16>& state) override {
        if (fail) return false;
        writes.push_back(state);
        held = state;
        return true;
    }
    bool read_output(std::bitset<16>& state) override {
        state = held;
        return true;
    }
};

json::object cmd(const char* text) {
    return json::parse(text).as_object();
}

StatsIdentity identity() {
    StatsIdentity id;
    id.node = "novaTest";
    id.build_id = "novaTest";
    return id;
}

const char* kLiveBeat = R"({"online":true,"seq":1,"time_ms":1})";

// The whole chain main() builds: controllers -> supervised wrappers -> fake
// drivers, with a clock the test moves by hand.
struct Fixture {
    steady_clock::time_point now{seconds(1000)};
    std::int64_t wall_ms = 1795110000000;

    StatsRegistry stats{identity(), true};
    FakeDigitalIo io;
    FakeRelayBank relays;
    OutputSupervisor supervisor{[this] { return now; }, [this] { return wall_ms; }};
    SupervisedDigitalIo supervised_io{io, supervisor, &stats};
    SupervisedRelayBank supervised_relays{relays, supervisor, std::bitset<16>(0xFFFF), &stats};
    TelemetryStore telemetry;
    GpioController gpio{&supervised_io, nullptr, &stats};
    RelayController relay{&supervised_relays, &telemetry, nullptr};

    Fixture() {
        supervisor.set_stats(&stats);
        gpio.set_supervisor(&supervisor);
        relay.set_supervisor(&supervisor);
        relay.set_initial_state(std::bitset<16>(0xFFFF));
        supervisor.set_trip_handler([this](const OutputId& output, const std::string& reason) {
            if (output.kind == OutputKind::Gpio) {
                gpio.drive_safe(output.index, reason);
            } else {
                relay.drive_safe(output.index, reason);
            }
        });
    }

    void advance(milliseconds ms) { now += ms; }

    // GPIO 17: max 1000 ms, safe on comms loss. GPIO 27: hold, no max.
    // Relay 2: max 30 s, safe. Relay 5: hold. Heartbeat 2000 ms.
    void configure(std::optional<std::int64_t> heartbeat = 2000) {
        gpio.apply({17, 27}, {});
        std::map<OutputId, OutputLimits> limits;
        limits[gpio_output(17)] = OutputLimits{1000, true};
        limits[gpio_output(27)] = OutputLimits{std::nullopt, false};
        limits[relay_output(2)] = OutputLimits{30000, true};
        limits[relay_output(5)] = OutputLimits{std::nullopt, false};
        supervisor.configure(limits, heartbeat);
    }

    void gpio_on(int pin) {
        gpio.handle_command(cmd(("{\"id\":" + std::to_string(pin) + ",\"state\":true}").c_str()));
    }
    void relay_energize(int channel) {
        relay.handle_command(cmd(("{\"id\":" + std::to_string(channel) + ",\"state\":0}").c_str()));
    }

    std::vector<std::string> trip_list() {
        std::vector<std::string> out;
        for (const auto& t : supervisor.tripped()) out.push_back(t.output.name() + "=" + t.reason);
        return out;
    }

    std::int64_t counter(const char* name) {
        return json::parse(stats.take_stats_json()).as_object().at(name).to_number<std::int64_t>();
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(output_supervisor, Fixture)

// ── max on-time ──────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(an_output_past_its_max_on_time_returns_to_safe) {
    configure(std::nullopt);
    gpio_on(17);
    advance(milliseconds(999));
    supervisor.tick();
    BOOST_TEST(io.levels[17] == 1);
    BOOST_TEST(supervisor.tripped().empty());

    advance(milliseconds(1));
    supervisor.tick();
    BOOST_TEST(io.levels[17] == 0);
    BOOST_TEST((trip_list() == std::vector<std::string>{"gpio:17=max_on_time"}));
    BOOST_TEST(supervisor.tripped()[0].time_ms == wall_ms);
    BOOST_TEST(supervisor.take_changed());
    BOOST_TEST(!supervisor.take_changed());
    BOOST_TEST(counter("output_trips") == 1);
}

BOOST_AUTO_TEST_CASE(the_max_on_time_counts_from_when_the_output_last_left_safe) {
    configure(std::nullopt);
    gpio_on(17);
    advance(milliseconds(800));
    gpio_on(17);  // already on: does not restart the clock
    advance(milliseconds(200));
    supervisor.tick();
    BOOST_TEST(io.levels[17] == 0);

    // Off and on again: a fresh second.
    gpio.handle_command(cmd(R"({"id":17,"state":false})"));
    gpio_on(17);
    advance(milliseconds(900));
    supervisor.tick();
    BOOST_TEST(io.levels[17] == 1);
}

BOOST_AUTO_TEST_CASE(an_output_without_a_max_on_time_stays_on) {
    configure(std::nullopt);
    gpio_on(27);
    advance(hours(10));
    supervisor.tick();
    BOOST_TEST(io.levels[27] == 1);
    BOOST_TEST(supervisor.tripped().empty());
}

BOOST_AUTO_TEST_CASE(a_relay_past_its_max_on_time_is_de_energized) {
    configure(std::nullopt);
    relay_energize(2);
    relay_energize(5);
    BOOST_TEST(!relays.held.test(2));
    advance(seconds(30));
    supervisor.tick();
    BOOST_TEST(relays.held.test(2));   // de-energized
    BOOST_TEST(!relays.held.test(5));  // no limit: untouched
    BOOST_TEST(telemetry.snapshot_relay_state().test(2));
    BOOST_TEST((trip_list() == std::vector<std::string>{"relay:2=max_on_time"}));
}

BOOST_AUTO_TEST_CASE(a_relay_command_after_a_trip_does_not_re_energize_the_tripped_channel) {
    configure(std::nullopt);
    relay_energize(2);
    advance(seconds(30));
    supervisor.tick();
    BOOST_TEST(relays.held.test(2));
    relay_energize(5);  // writes all 16 channels at once
    BOOST_TEST(relays.held.test(2));
    BOOST_TEST(!relays.held.test(5));
}

// ── latching ─────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(a_trip_latches_until_the_next_command_for_that_output) {
    configure(std::nullopt);
    gpio_on(17);
    gpio_on(27);
    advance(seconds(1));
    supervisor.tick();
    BOOST_TEST((trip_list() == std::vector<std::string>{"gpio:17=max_on_time"}));
    supervisor.take_changed();

    // More ticks, and a command for another output, change nothing.
    advance(seconds(5));
    supervisor.tick();
    gpio.handle_command(cmd(R"({"id":27,"state":false})"));
    BOOST_TEST(io.levels[17] == 0);
    BOOST_TEST(trip_list().size() == 1u);
    BOOST_TEST(!supervisor.take_changed());

    // A command for it clears it, even one to its safe level.
    gpio.handle_command(cmd(R"({"id":17,"state":false})"));
    BOOST_TEST(supervisor.tripped().empty());
    BOOST_TEST(supervisor.take_changed());
}

BOOST_AUTO_TEST_CASE(a_relay_trip_clears_on_a_command_for_that_channel_even_without_a_change) {
    configure(std::nullopt);
    relay_energize(2);
    advance(seconds(30));
    supervisor.tick();
    BOOST_TEST(trip_list().size() == 1u);
    relay.handle_command(cmd(R"({"id":2,"state":1})"));  // already de-energized
    BOOST_TEST(supervisor.tripped().empty());
}

BOOST_AUTO_TEST_CASE(a_released_pin_forgets_its_trip) {
    configure(std::nullopt);
    gpio_on(17);
    advance(seconds(1));
    supervisor.tick();
    BOOST_TEST(trip_list().size() == 1u);
    gpio.apply({27}, {});  // 17 dropped from the config
    BOOST_TEST(supervisor.tripped().empty());
}

// ── comms loss ───────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(no_heartbeat_within_the_timeout_trips_safe_outputs_only) {
    configure();
    supervisor.heartbeat(kLiveBeat, false);
    gpio_on(17);
    gpio_on(27);
    relay_energize(2);
    relay_energize(5);

    advance(milliseconds(2000));
    BOOST_TEST(!supervisor.comms_stale());
    supervisor.tick();  // (GPIO 17 trips here on its 1000 ms max on-time)
    BOOST_TEST(!relays.held.test(2));

    advance(milliseconds(1));
    BOOST_TEST(supervisor.comms_stale());
    supervisor.tick();
    BOOST_TEST(io.levels[27] == 1);    // hold
    BOOST_TEST(!relays.held.test(5));  // hold
    BOOST_TEST(relays.held.test(2));   // safe: de-energized
    BOOST_TEST((trip_list() == std::vector<std::string>{"gpio:17=max_on_time", "relay:2=comms_loss"}));
}

BOOST_AUTO_TEST_CASE(comms_loss_trips_with_reason_comms_loss) {
    configure();
    gpio.apply({4}, {});
    std::map<OutputId, OutputLimits> limits;
    limits[gpio_output(4)] = OutputLimits{std::nullopt, true};
    supervisor.configure(limits, 2000);
    supervisor.heartbeat(kLiveBeat, false);
    gpio_on(4);
    advance(milliseconds(2001));
    supervisor.tick();
    BOOST_TEST(io.levels[4] == 0);
    BOOST_TEST((trip_list() == std::vector<std::string>{"gpio:4=comms_loss"}));
}

BOOST_AUTO_TEST_CASE(beats_keep_it_alive) {
    configure();
    relay_energize(2);
    for (int i = 0; i < 40; ++i) {
        advance(milliseconds(250));
        supervisor.heartbeat(kLiveBeat, false);
        supervisor.tick();
    }
    BOOST_TEST(!relays.held.test(2));
    BOOST_TEST(supervisor.tripped().empty());
}

BOOST_AUTO_TEST_CASE(retained_heartbeats_do_not_count_as_beats) {
    configure();
    relay_energize(2);
    for (int i = 0; i < 12; ++i) {
        advance(milliseconds(250));
        supervisor.heartbeat(R"({"online":true})", true);
        supervisor.heartbeat(kLiveBeat, true);
        supervisor.tick();
    }
    BOOST_TEST(relays.held.test(2));
    BOOST_TEST((trip_list() == std::vector<std::string>{"relay:2=comms_loss"}));
}

BOOST_AUTO_TEST_CASE(the_last_will_trips_at_once_retained_or_not) {
    for (bool retained : {false, true}) {
        OutputSupervisor sup([this] { return now; }, [this] { return wall_ms; });
        sup.configure({{relay_output(2), OutputLimits{std::nullopt, true}}}, 2000);
        sup.note_level(relay_output(2), false);
        sup.heartbeat(kLiveBeat, false);
        BOOST_TEST(!sup.comms_stale());
        sup.heartbeat(R"({"online":false})", retained);
        BOOST_TEST(sup.comms_stale());
        // Only a live beat ends it.
        sup.heartbeat(R"({"online":true})", true);
        BOOST_TEST(sup.comms_stale());
        sup.heartbeat(kLiveBeat, false);
        BOOST_TEST(!sup.comms_stale());
    }
}

BOOST_AUTO_TEST_CASE(an_mqtt_connection_loss_is_a_comms_loss) {
    configure();
    supervisor.heartbeat(kLiveBeat, false);
    relay_energize(2);
    supervisor.connection_lost();
    supervisor.tick();
    BOOST_TEST(relays.held.test(2));
    BOOST_TEST((trip_list() == std::vector<std::string>{"relay:2=comms_loss"}));
    supervisor.heartbeat(kLiveBeat, false);
    BOOST_TEST(!supervisor.comms_stale());
}

BOOST_AUTO_TEST_CASE(malformed_heartbeats_are_ignored) {
    configure();
    supervisor.heartbeat("not json", false);
    supervisor.heartbeat(R"({"online":"false"})", false);
    supervisor.heartbeat(R"([false])", false);
    BOOST_TEST(!supervisor.comms_stale());
}

BOOST_AUTO_TEST_CASE(comms_supervision_starts_only_with_a_heartbeat_section) {
    // Relay 2 set to `safe` on comms loss, with no max on-time.
    supervisor.configure({{relay_output(2), OutputLimits{std::nullopt, true}}}, std::nullopt);
    relay_energize(2);
    supervisor.connection_lost();
    advance(hours(1));
    supervisor.tick();
    BOOST_TEST(!supervisor.comms_stale());
    BOOST_TEST(!relays.held.test(2));

    // Before any config at all, nothing is supervised either.
    OutputSupervisor fresh([this] { return now; }, [this] { return wall_ms; });
    fresh.connection_lost();
    BOOST_TEST(!fresh.comms_stale());
}

BOOST_AUTO_TEST_CASE(supervision_starts_with_a_full_timeout_of_grace) {
    configure(std::nullopt);
    advance(hours(1));
    configure(2000);  // the heartbeat section arrives now
    BOOST_TEST(!supervisor.comms_stale());
    advance(milliseconds(2000));
    BOOST_TEST(!supervisor.comms_stale());
    advance(milliseconds(1));
    BOOST_TEST(supervisor.comms_stale());
}

BOOST_AUTO_TEST_CASE(a_config_without_a_heartbeat_section_turns_supervision_off) {
    configure(2000);
    supervisor.connection_lost();
    BOOST_TEST(supervisor.comms_stale());
    configure(std::nullopt);
    BOOST_TEST(!supervisor.comms_stale());
}

// ── refusal while stale ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(commands_moving_a_safe_output_off_safe_are_refused_while_stale) {
    configure();
    supervisor.connection_lost();
    io.calls.clear();
    gpio_on(17);    // safe: refused
    relay_energize(2);  // safe: refused
    BOOST_TEST(io.levels[17] == 0);
    BOOST_TEST(relays.held.test(2));
    BOOST_TEST(io.calls.empty());
    BOOST_TEST(relays.writes.empty());

    // hold outputs still move, and safe outputs can still go to safe.
    gpio_on(27);
    relay_energize(5);
    gpio.handle_command(cmd(R"({"id":17,"state":false})"));
    relay.handle_command(cmd(R"({"id":2,"state":1})"));
    BOOST_TEST(io.levels[27] == 1);
    BOOST_TEST(!relays.held.test(5));
    BOOST_TEST(relays.held.test(2));
    BOOST_TEST(counter("refused_commands") == 2);

    // Back to normal after a live beat.
    supervisor.heartbeat(kLiveBeat, false);
    gpio_on(17);
    relay_energize(2);
    BOOST_TEST(io.levels[17] == 1);
    BOOST_TEST(!relays.held.test(2));
}

BOOST_AUTO_TEST_CASE(a_refused_relay_command_is_not_carried_into_the_next_write) {
    configure();
    supervisor.connection_lost();
    relay_energize(2);  // refused
    supervisor.heartbeat(kLiveBeat, false);
    relay_energize(5);
    BOOST_TEST(relays.held.test(2));  // 2 stays de-energized
    BOOST_TEST(!relays.held.test(5));
}

BOOST_AUTO_TEST_CASE(a_refused_command_does_not_clear_a_trip) {
    configure();
    supervisor.heartbeat(kLiveBeat, false);
    relay_energize(2);
    supervisor.connection_lost();
    supervisor.tick();
    BOOST_TEST(trip_list().size() == 1u);
    relay_energize(2);  // refused
    BOOST_TEST(trip_list().size() == 1u);
}

BOOST_AUTO_TEST_CASE(a_trip_stays_latched_after_the_heartbeat_returns) {
    configure();
    supervisor.heartbeat(kLiveBeat, false);
    relay_energize(2);
    supervisor.connection_lost();
    supervisor.tick();
    supervisor.heartbeat(kLiveBeat, false);
    advance(milliseconds(250));
    supervisor.tick();
    BOOST_TEST(relays.held.test(2));
    BOOST_TEST((trip_list() == std::vector<std::string>{"relay:2=comms_loss"}));
}

// ── failures and --stop-actuate ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(a_failed_trip_write_is_retried_but_not_on_every_tick) {
    configure(std::nullopt);
    gpio_on(17);
    io.failing_writes.insert(17);
    advance(seconds(1));
    io.calls.clear();
    supervisor.tick();
    supervisor.tick();
    advance(milliseconds(500));
    supervisor.tick();
    BOOST_TEST(io.calls.size() == 1u);  // one attempt so far
    BOOST_TEST(supervisor.tripped().empty());

    io.failing_writes.clear();
    advance(milliseconds(500));
    supervisor.tick();
    BOOST_TEST(io.levels[17] == 0);
    BOOST_TEST((trip_list() == std::vector<std::string>{"gpio:17=max_on_time"}));
}

BOOST_AUTO_TEST_CASE(under_stop_actuate_a_trip_moves_nothing) {
    // controllers -> supervised -> stopped -> fake, as main() builds it.
    FakeDigitalIo raw;
    StoppedDigitalIo stopped(raw, nullptr);
    OutputSupervisor sup([this] { return now; }, [this] { return wall_ms; });
    SupervisedDigitalIo wrapped(stopped, sup);
    GpioController controller(&wrapped, nullptr);
    controller.set_supervisor(&sup);
    sup.set_trip_handler([&controller](const OutputId& o, const std::string& r) { controller.drive_safe(o.index, r); });

    controller.apply({17}, {});
    sup.configure({{gpio_output(17), OutputLimits{1000, true}}}, std::nullopt);
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    advance(seconds(1));
    sup.tick();
    BOOST_TEST(raw.calls.empty());
    BOOST_TEST((sup.tripped().size() == 1u));
}

BOOST_AUTO_TEST_CASE(output_names_are_the_status_form) {
    BOOST_TEST(gpio_output(17).name() == "gpio:17");
    BOOST_TEST(relay_output(3).name() == "relay:3");
}

BOOST_AUTO_TEST_SUITE_END()
