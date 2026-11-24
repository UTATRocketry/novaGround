#include <boost/test/unit_test.hpp>

#include <bitset>
#include <boost/json.hpp>
#include <map>
#include <string>
#include <vector>

#include "controllers/gpio_controller.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/servo_controller.hpp"
#include "core/actuation_guard.hpp"
#include "core/command_router.hpp"
#include "core/stats.hpp"
#include "core/telemetry.hpp"
#include "fake_digital_io.hpp"

namespace json = boost::json;

namespace {
class FakeRelayBank : public RelayBank {
public:
    std::vector<std::bitset<16>> writes;
    std::bitset<16> held{0xFFFF};  // what the "hardware" holds

    bool is_ready() const override { return true; }
    bool write_output(const std::bitset<16>& state) override {
        writes.push_back(state);
        held = state;
        return true;
    }
    bool read_output(std::bitset<16>& state) override {
        state = held;
        return true;
    }
};

class FakePwm : public PwmDriver {
public:
    std::vector<std::string> calls;  // "us:<ch>:<value>" and "off:<ch>"

    bool is_ready() const override { return true; }
    void writeMicroseconds(uint8_t channel, uint16_t us) override {
        calls.push_back("us:" + std::to_string(channel) + ":" + std::to_string(us));
    }
    bool disablePWM(uint8_t channel) override {
        calls.push_back("off:" + std::to_string(channel));
        return true;
    }
    bool read_channel(uint8_t channel, PwmChannelReadback& readback) override {
        readback.output = PwmOutput::Pulse;
        readback.microseconds = 1234;
        return channel < 16;
    }
};

json::object cmd(const char* text) {
    return json::parse(text).as_object();
}

// The count since the start, from the stats message.
double stopped_writes(StatsRegistry& stats) {
    auto v = json::parse(stats.take_stats_json()).as_object().at("stopped_writes");
    return static_cast<double>(v.to_number<std::int64_t>());
}

StatsIdentity identity() {
    StatsIdentity id;
    id.node = "novaTest";
    id.build_id = "novaTest";
    id.actuation = "stopped";
    return id;
}

struct Fixture {
    StatsRegistry stats{identity(), true};
    FakeRelayBank relays;
    FakePwm servo;
    FakeDigitalIo gpio;
    StoppedRelayBank stopped_relays{relays, &stats};
    StoppedPwmDriver stopped_servo{servo, &stats};
    StoppedDigitalIo stopped_gpio{gpio, &stats};
    TelemetryStore telemetry;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(actuation_guard, Fixture)

BOOST_AUTO_TEST_CASE(a_stopped_relay_bank_drops_the_write_and_says_it_succeeded) {
    std::bitset<16> all_off;
    BOOST_TEST(stopped_relays.write_output(all_off));
    BOOST_TEST(relays.writes.empty());
    BOOST_TEST(relays.held.all());
    BOOST_TEST(stopped_writes(stats) == 1.0);
}

BOOST_AUTO_TEST_CASE(a_stopped_pwm_driver_drops_both_kinds_of_write) {
    stopped_servo.writeMicroseconds(2, 1500);
    BOOST_TEST(stopped_servo.disablePWM(2));
    BOOST_TEST(servo.calls.empty());
    BOOST_TEST(stopped_writes(stats) == 2.0);
}

BOOST_AUTO_TEST_CASE(stopped_digital_io_drops_writes_and_output_claims) {
    BOOST_TEST(stopped_gpio.configure_output(17, 0));
    BOOST_TEST(stopped_gpio.write(17, 1));
    BOOST_TEST(gpio.calls.empty());
    BOOST_TEST(stopped_writes(stats) == 2.0);
}

BOOST_AUTO_TEST_CASE(stopped_digital_io_passes_input_claims_and_releases) {
    // Neither drives a line; the GPIO sampler needs the inputs.
    BOOST_TEST(stopped_gpio.configure_input(5));
    BOOST_TEST(stopped_gpio.release(5));
    BOOST_TEST((gpio.calls == std::vector<std::string>{"in:5", "release:5"}));
    BOOST_TEST(stopped_writes(stats) == 0.0);
}

BOOST_AUTO_TEST_CASE(reads_still_go_to_the_real_driver) {
    // What the hardware holds is what is reported, not what was commanded.
    relays.held = std::bitset<16>(0x00F0);
    std::bitset<16> state;
    BOOST_TEST(stopped_relays.read_output(state));
    BOOST_TEST(state.to_ulong() == 0x00F0ul);
    BOOST_TEST(stopped_relays.is_ready());

    PwmChannelReadback readback;
    BOOST_TEST(stopped_servo.read_channel(3, readback));
    BOOST_TEST(readback.microseconds == 1234);
    BOOST_TEST(!stopped_servo.read_channel(16, readback));

    gpio.directions[5] = "in";
    gpio.directions[17] = "out";
    gpio.levels[5] = 1;
    gpio.levels[17] = 1;
    BOOST_TEST((stopped_gpio.get_input_pins() == std::vector<int>{5}));
    BOOST_TEST(stopped_gpio.read_all_inputs().at(5) == 1);
    BOOST_TEST(stopped_gpio.read_all_outputs().at(17) == 1);
    int level = 0;
    BOOST_TEST(stopped_gpio.read(5, level));
    BOOST_TEST(level == 1);
    BOOST_TEST(stopped_writes(stats) == 0.0);  // reads are not counted
}

BOOST_AUTO_TEST_CASE(it_works_without_a_stats_registry) {
    StoppedRelayBank bare(relays, nullptr);
    BOOST_TEST(bare.write_output(std::bitset<16>()));
    BOOST_TEST(relays.writes.empty());
}

// The controllers run in full on top of the wrappers: they validate, update
// telemetry and keep their own state, and only the driver write disappears.
BOOST_AUTO_TEST_CASE(a_relay_command_is_handled_but_nothing_is_written) {
    RelayController controller(&stopped_relays, &telemetry, nullptr);
    controller.handle_command(cmd(R"({"type":"relay","id":3,"state":true})"));
    BOOST_TEST(telemetry.snapshot_relay_state().to_ulong() == 1ul << 3);
    BOOST_TEST(relays.writes.empty());
    BOOST_TEST(relays.held.all());
    BOOST_TEST(stopped_writes(stats) == 1.0);
}

BOOST_AUTO_TEST_CASE(a_malformed_relay_command_is_still_rejected_by_the_controller) {
    RelayController controller(&stopped_relays, &telemetry, nullptr);
    controller.handle_command(cmd(R"({"type":"relay","id":99,"state":true})"));
    controller.handle_command(cmd(R"({"type":"relay","state":true})"));
    BOOST_TEST(telemetry.snapshot_relay_state().none());
    BOOST_TEST(stopped_writes(stats) == 0.0);  // never reached the driver
}

BOOST_AUTO_TEST_CASE(a_servo_command_is_handled_but_nothing_is_written) {
    ServoController controller(&stopped_servo, &telemetry, nullptr);
    controller.handle_command(cmd(R"({"type":"servo","id":2,"angle":1700})"));
    controller.handle_command(cmd(R"({"type":"servo","id":2,"angle":"off"})"));
    const auto states = telemetry.snapshot_servo_states();
    BOOST_REQUIRE_EQUAL(states.size(), 1u);
    BOOST_TEST(states[0].angle == 1700);
    BOOST_TEST(!states[0].enabled);
    BOOST_TEST(servo.calls.empty());
    BOOST_TEST(stopped_writes(stats) == 2.0);
}

BOOST_AUTO_TEST_CASE(a_gpio_command_is_handled_but_nothing_is_written) {
    GpioController controller(&stopped_gpio, nullptr);
    controller.apply({17}, {});
    controller.handle_command(cmd(R"({"type":"gpio","id":17,"state":true})"));
    BOOST_TEST(gpio.calls.empty());
    BOOST_TEST(stopped_writes(stats) == 2.0);  // the claim and the write
}

BOOST_AUTO_TEST_CASE(the_command_path_is_still_timed) {
    RelayController relay_controller(&stopped_relays, &telemetry, nullptr);
    CommandRouter router("novaOps");
    router.register_handler("relay", [&](const json::object& c) { relay_controller.handle_command(c); });
    router.set_stats(&stats);
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":1,"state":false}})", CommandOrigin::Node);

    auto msg = json::parse(stats.take_stats_json()).as_object();
    const auto& relay = msg.at("commands").at("relay").as_object();
    BOOST_TEST(relay.at("count").to_number<std::int64_t>() == 1);
    BOOST_TEST(relays.writes.empty());
    BOOST_TEST(msg.at("stopped_writes").to_number<std::int64_t>() == 1);
}

BOOST_AUTO_TEST_SUITE_END()
