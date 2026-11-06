#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "controllers/servo_controller.hpp"
#include "core/telemetry.hpp"
#include "drivers/servo.hpp"
#include "fake_i2c_bus.hpp"

namespace json = boost::json;

namespace {
class FakePwm : public PwmDriver {
public:
    bool ready = true;
    std::vector<std::string> calls;  // "us:<ch>:<value>" and "off:<ch>"

    bool is_ready() const override { return ready; }
    void writeMicroseconds(uint8_t ch, uint16_t us) override {
        calls.push_back("us:" + std::to_string(ch) + ":" + std::to_string(us));
    }
    bool disablePWM(uint8_t ch) override {
        calls.push_back("off:" + std::to_string(ch));
        return true;
    }
};

json::object cmd(const char* text) {
    return json::parse(text).as_object();
}

struct Fixture {
    FakePwm pwm;
    TelemetryStore telemetry;
    ServoController controller{&pwm, &telemetry, nullptr};

    ServoTelemetry servo(int id) {
        for (const auto& s : telemetry.snapshot_servo_states()) {
            if (s.id == id) return s;
        }
        return ServoTelemetry{-1, 0, false};
    }
};
}

BOOST_FIXTURE_TEST_SUITE(servo_controller, Fixture)

BOOST_AUTO_TEST_CASE(a_numeric_angle_is_a_pulse_width_in_microseconds) {
    controller.handle_command(cmd(R"({"id":3,"angle":1200})"));
    BOOST_REQUIRE_EQUAL(pwm.calls.size(), 1u);
    BOOST_TEST(pwm.calls[0] == "us:3:1200");
    BOOST_TEST(servo(3).angle == 1200);
    BOOST_TEST(servo(3).enabled);
}

BOOST_AUTO_TEST_CASE(off_disables_the_channel_and_keeps_the_last_angle) {
    controller.handle_command(cmd(R"({"id":3,"angle":1200})"));
    controller.handle_command(cmd(R"({"id":3,"angle":"off"})"));
    BOOST_REQUIRE_EQUAL(pwm.calls.size(), 2u);
    BOOST_TEST(pwm.calls[1] == "off:3");
    BOOST_TEST(servo(3).angle == 1200);
    BOOST_TEST(!servo(3).enabled);
}

BOOST_AUTO_TEST_CASE(on_restores_the_last_angle) {
    controller.handle_command(cmd(R"({"id":3,"angle":1200})"));
    controller.handle_command(cmd(R"({"id":3,"angle":"off"})"));
    controller.handle_command(cmd(R"({"id":3,"angle":"on"})"));
    BOOST_REQUIRE_EQUAL(pwm.calls.size(), 3u);
    BOOST_TEST(pwm.calls[2] == "us:3:1200");
    BOOST_TEST(servo(3).enabled);
}

BOOST_AUTO_TEST_CASE(on_with_no_previous_angle_uses_1500_microseconds) {
    controller.handle_command(cmd(R"({"id":7,"angle":"on"})"));
    BOOST_REQUIRE_EQUAL(pwm.calls.size(), 1u);
    BOOST_TEST(pwm.calls[0] == "us:7:1500");
}

BOOST_AUTO_TEST_CASE(on_and_off_are_case_insensitive) {
    controller.handle_command(cmd(R"({"id":1,"angle":"ON"})"));
    controller.handle_command(cmd(R"({"id":1,"angle":"Off"})"));
    BOOST_TEST((pwm.calls == std::vector<std::string>{"us:1:1500", "off:1"}));
}

BOOST_AUTO_TEST_CASE(servos_keep_independent_state) {
    controller.handle_command(cmd(R"({"id":1,"angle":1100})"));
    controller.handle_command(cmd(R"({"id":2,"angle":1900})"));
    controller.handle_command(cmd(R"({"id":1,"angle":"off"})"));
    BOOST_TEST(!servo(1).enabled);
    BOOST_TEST(servo(2).enabled);
    BOOST_TEST(servo(2).angle == 1900);
}

BOOST_AUTO_TEST_CASE(ids_0_and_15_are_valid_and_others_are_not) {
    controller.handle_command(cmd(R"({"id":0,"angle":1500})"));
    controller.handle_command(cmd(R"({"id":15,"angle":1500})"));
    BOOST_TEST(pwm.calls.size() == 2u);
    controller.handle_command(cmd(R"({"id":-1,"angle":1500})"));
    controller.handle_command(cmd(R"({"id":16,"angle":1500})"));
    BOOST_TEST(pwm.calls.size() == 2u);
}

BOOST_AUTO_TEST_CASE(bad_commands_drive_nothing) {
    controller.handle_command(cmd(R"({"angle":1500})"));
    controller.handle_command(cmd(R"({"id":1})"));
    controller.handle_command(cmd(R"({"id":"1","angle":1500})"));
    controller.handle_command(cmd(R"({"id":1,"angle":"sideways"})"));
    controller.handle_command(cmd(R"({"id":1,"angle":null})"));
    controller.handle_command(cmd(R"({"id":1,"angle":[1500]})"));
    BOOST_TEST(pwm.calls.empty());
    BOOST_TEST(telemetry.snapshot_servo_states().empty());
}

BOOST_AUTO_TEST_CASE(unavailable_hardware_drives_nothing) {
    pwm.ready = false;
    controller.handle_command(cmd(R"({"id":1,"angle":1500})"));
    BOOST_TEST(pwm.calls.empty());

    ServoController none(nullptr, &telemetry, nullptr);
    none.handle_command(cmd(R"({"id":1,"angle":1500})"));
    BOOST_TEST(telemetry.snapshot_servo_states().empty());
}

// The real PCA9685 driver behind the controller, on a fake bus: a command ends
// up as the right register bytes.
BOOST_AUTO_TEST_CASE(controller_to_register_bytes_through_the_real_driver) {
    FakeI2cBus bus;
    Adafruit_PWMServoDriver chip(bus);
    BOOST_REQUIRE(chip.begin());
    chip.setPWMFreq(50);
    ServoController real(&chip, &telemetry, nullptr);

    real.handle_command(cmd(R"({"id":1,"angle":1500})"));
    BOOST_TEST(bus.reg(0x40, 0x0A + 2) == 0x33);
    BOOST_TEST(bus.reg(0x40, 0x0A + 3) == 0x01);

    real.handle_command(cmd(R"({"id":1,"angle":"off"})"));
    BOOST_TEST(bus.reg(0x40, 0x0A + 3) == 0x10);
}

BOOST_AUTO_TEST_SUITE_END()
