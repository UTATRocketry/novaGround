#include <boost/test/unit_test.hpp>

#include <boost/json.hpp>
#include <chrono>
#include <stdexcept>
#include <string>

#include "core/actuator_state.hpp"
#include "core/stats.hpp"
#include "fake_digital_io.hpp"
#include "sim/sim_actuators.hpp"

namespace json = boost::json;

namespace {
json::object parse(const std::string& text) {
    return json::parse(text).as_object();
}

double number(const json::value& v) {
    return v.is_double() ? v.as_double() : static_cast<double>(v.to_number<std::int64_t>());
}

StatsRegistry make_stats() {
    StatsIdentity id;
    id.node = "novaMock";
    return StatsRegistry(id, true);
}

class FailingRelays : public RelayBank {
public:
    bool ready = true;
    bool throws = false;
    bool is_ready() const override { return ready; }
    bool write_output(const std::bitset<16>&) override { return true; }
    bool read_output(std::bitset<16>&) override {
        if (throws) throw std::runtime_error("bus on fire");
        return false;
    }
};

// Reads channel 4 wrong, the rest fine.
class OneBadChannel : public PwmDriver {
public:
    bool is_ready() const override { return true; }
    void writeMicroseconds(uint8_t, uint16_t) override {}
    bool disablePWM(uint8_t) override { return true; }
    bool read_channel(uint8_t channel, PwmChannelReadback& readback) override {
        if (channel == 4) return false;
        readback = PwmChannelReadback{PwmOutput::On, 0};
        return true;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(actuator_state)

BOOST_AUTO_TEST_CASE(the_message_reports_what_each_driver_holds) {
    SimRelayBank relays;
    relays.write_output(std::bitset<16>(0x8005));
    SimPwmDriver servo;
    servo.writeMicroseconds(1, 1498);
    servo.writeMicroseconds(2, 1500);
    servo.disablePWM(2);
    SimDigitalIo gpio;
    gpio.set_direction(17, "out");
    gpio.set_direction(27, "out");
    gpio.set_direction(5, "in");
    gpio.write(27, 1);

    ActuatorSources sources;
    sources.relays = &relays;
    sources.servo = &servo;
    sources.gpio = &gpio;
    auto now = std::chrono::system_clock::time_point(std::chrono::milliseconds(1793000000123));
    auto msg = parse(actuator_state_json(sources, nullptr, now));

    BOOST_TEST(number(msg.at("schema")) == 1.0);
    BOOST_TEST(number(msg.at("time_ms")) == 1793000000123.0);

    const auto& r = msg.at("relays").as_object();
    BOOST_TEST(r.at("ok").as_bool());
    BOOST_TEST(number(r.at("mask")) == 0x8005);

    const auto& s = msg.at("servos").as_object();
    BOOST_TEST(s.at("ok").as_bool());
    const auto& channels = s.at("channels").as_array();
    BOOST_REQUIRE_EQUAL(channels.size(), static_cast<std::size_t>(kServoChannels));
    for (int id = 0; id < kServoChannels; ++id) {
        BOOST_TEST(number(channels[id].as_object().at("id")) == id);
    }
    const auto& ch0 = channels[0].as_object();
    BOOST_TEST(std::string(ch0.at("output").as_string().c_str()) == "off");
    BOOST_TEST(!ch0.contains("pulse_us"));
    const auto& ch1 = channels[1].as_object();
    BOOST_TEST(std::string(ch1.at("output").as_string().c_str()) == "pulse");
    BOOST_TEST(number(ch1.at("pulse_us")) == 1498.0);
    BOOST_TEST(std::string(channels[2].as_object().at("output").as_string().c_str()) == "off");

    const auto& g = msg.at("gpios").as_object();
    BOOST_TEST(g.at("ok").as_bool());
    const auto& outputs = g.at("outputs").as_array();
    BOOST_REQUIRE_EQUAL(outputs.size(), 2u);  // the two outputs, not the input
    BOOST_TEST(number(outputs[0].as_object().at("pin_id")) == 17.0);
    BOOST_TEST(number(outputs[0].as_object().at("state")) == 0.0);
    BOOST_TEST(number(outputs[1].as_object().at("pin_id")) == 27.0);
    BOOST_TEST(number(outputs[1].as_object().at("state")) == 1.0);
}

BOOST_AUTO_TEST_CASE(hardware_that_is_not_there_has_no_section) {
    SimRelayBank relays;
    ActuatorSources sources;
    sources.relays = &relays;
    auto msg = parse(actuator_state_json(sources, nullptr));
    BOOST_TEST(msg.contains("relays"));
    BOOST_TEST(!msg.contains("servos"));
    BOOST_TEST(!msg.contains("gpios"));

    auto none = parse(actuator_state_json(ActuatorSources{}, nullptr));
    BOOST_TEST(!none.contains("relays"));
    BOOST_TEST(none.contains("time_ms"));
}

BOOST_AUTO_TEST_CASE(a_relay_that_cannot_be_read_is_reported_and_counted) {
    StatsRegistry stats = make_stats();
    FailingRelays relays;
    ActuatorSources sources;
    sources.relays = &relays;

    auto msg = parse(actuator_state_json(sources, &stats));
    const auto& r = msg.at("relays").as_object();
    BOOST_TEST(!r.at("ok").as_bool());
    BOOST_TEST(!r.contains("mask"));  // no value rather than a made-up one

    relays.throws = true;  // an exception is handled the same way
    msg = parse(actuator_state_json(sources, &stats));
    BOOST_TEST(!msg.at("relays").as_object().at("ok").as_bool());

    relays.throws = false;
    relays.ready = false;  // not ready: not read at all
    msg = parse(actuator_state_json(sources, &stats));
    BOOST_TEST(!msg.at("relays").as_object().at("ok").as_bool());

    auto stats_msg = parse(stats.take_stats_json());
    BOOST_TEST(number(stats_msg.at("actuator_read_errors")) == 3.0);
}

BOOST_AUTO_TEST_CASE(one_unreadable_servo_channel_does_not_hide_the_others) {
    StatsRegistry stats = make_stats();
    OneBadChannel servo;
    ActuatorSources sources;
    sources.servo = &servo;
    auto msg = parse(actuator_state_json(sources, &stats));
    const auto& s = msg.at("servos").as_object();
    BOOST_TEST(!s.at("ok").as_bool());
    const auto& channels = s.at("channels").as_array();
    BOOST_REQUIRE_EQUAL(channels.size(), static_cast<std::size_t>(kServoChannels));
    BOOST_TEST(std::string(channels[4].as_object().at("output").as_string().c_str()) == "unreadable");
    BOOST_TEST(std::string(channels[3].as_object().at("output").as_string().c_str()) == "on");
    BOOST_TEST(std::string(channels[5].as_object().at("output").as_string().c_str()) == "on");
    BOOST_TEST(number(parse(stats.take_stats_json()).at("actuator_read_errors")) == 1.0);
}

BOOST_AUTO_TEST_CASE(a_gpio_driver_that_throws_is_reported_and_counted) {
    StatsRegistry stats = make_stats();
    FakeDigitalIo gpio;
    gpio.set_direction(17, "out");
    gpio.throw_on_read_all = true;
    ActuatorSources sources;
    sources.gpio = &gpio;
    auto msg = parse(actuator_state_json(sources, &stats));
    const auto& g = msg.at("gpios").as_object();
    BOOST_TEST(!g.at("ok").as_bool());
    BOOST_TEST(g.at("outputs").as_array().empty());
    BOOST_TEST(number(parse(stats.take_stats_json()).at("actuator_read_errors")) == 1.0);
}

BOOST_AUTO_TEST_CASE(reading_changes_nothing) {
    SimRelayBank relays;
    relays.write_output(std::bitset<16>(0x00F0));
    SimPwmDriver servo;
    servo.writeMicroseconds(7, 1200);
    SimDigitalIo gpio;
    gpio.set_direction(22, "out");
    gpio.write(22, 1);
    ActuatorSources sources;
    sources.relays = &relays;
    sources.servo = &servo;
    sources.gpio = &gpio;

    const auto first = actuator_state_json(sources, nullptr, std::chrono::system_clock::time_point{});
    const auto second = actuator_state_json(sources, nullptr, std::chrono::system_clock::time_point{});
    BOOST_TEST(first == second);
    BOOST_TEST(relays.writes() == 1);  // only the setup write
    BOOST_TEST(servo.microseconds(7) == 1200);
    BOOST_TEST(gpio.output_level(22) == 1);
}

BOOST_AUTO_TEST_SUITE_END()
