#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <string>

#include "drivers/servo.hpp"
#include "fake_i2c_bus.hpp"

namespace {
constexpr uint8_t kAddr = 0x40;
constexpr uint8_t kPrescale = 0xFE;

struct Fixture {
    FakeI2cBus bus;
    Adafruit_PWMServoDriver pwm{bus};

    // Bring the chip up the way main() does, then forget those writes.
    void start_at_50hz() {
        BOOST_REQUIRE(pwm.begin());
        pwm.setPWMFreq(50);
        bus.writes.clear();
    }
};

// "reg=value" for each write, in order, e.g. "00=80 FE=05".
std::string sequence(const FakeI2cBus& bus) {
    std::string out;
    for (const auto& w : bus.writes) {
        char item[16];
        std::snprintf(item, sizeof item, "%s%02X=%02X", out.empty() ? "" : " ", w.reg, w.value);
        out += item;
    }
    return out;
}
}

BOOST_FIXTURE_TEST_SUITE(pca9685, Fixture)

BOOST_AUTO_TEST_CASE(begin_resets_then_sets_1000_hz_and_wakes_with_auto_increment) {
    BOOST_TEST(pwm.begin());
    BOOST_TEST(pwm.is_ready());
    BOOST_TEST(bus.attached.at(0) == kAddr);
    // reset(); then setPWMFreq(1000): sleep, prescale 5, restore, restart+AI.
    BOOST_TEST(sequence(bus) == "00=80 00=10 FE=05 00=80 00=A0");
    for (const auto& w : bus.writes) BOOST_TEST(w.address == kAddr);
}

BOOST_AUTO_TEST_CASE(begin_fails_without_touching_the_chip_when_the_bus_is_unusable) {
    bus.ready = false;
    BOOST_TEST(!pwm.begin());
    BOOST_TEST(!pwm.is_ready());
    BOOST_TEST(bus.writes.empty());

    FakeI2cBus no_addr;
    no_addr.attach_ok = false;
    Adafruit_PWMServoDriver other(no_addr);
    BOOST_TEST(!other.begin());
    BOOST_TEST(no_addr.writes.empty());
}

BOOST_AUTO_TEST_CASE(a_driver_that_was_never_started_ignores_everything) {
    BOOST_TEST(!pwm.is_ready());
    pwm.setPWM(0, 0, 300);
    pwm.writeMicroseconds(0, 1500);
    pwm.setPWMFreq(50);
    BOOST_TEST(!pwm.disablePWM(0));
    BOOST_TEST(bus.writes.empty());
}

BOOST_AUTO_TEST_CASE(not_ready_when_the_bus_goes_away) {
    BOOST_REQUIRE(pwm.begin());
    bus.ready = false;
    BOOST_TEST(!pwm.is_ready());
}

BOOST_AUTO_TEST_CASE(set_pwm_freq_50hz_uses_prescale_121_in_the_sleep_restart_sequence) {
    BOOST_REQUIRE(pwm.begin());  // leaves MODE1 at 0xA0
    bus.writes.clear();
    pwm.setPWMFreq(50);
    BOOST_TEST(sequence(bus) == "00=30 FE=79 00=A0 00=A0");
}

BOOST_AUTO_TEST_CASE(frequency_is_clamped_to_the_chip_range) {
    BOOST_REQUIRE(pwm.begin());
    pwm.setPWMFreq(0.1f);  // clamps to 1 Hz: prescale clamps to 255
    BOOST_TEST(bus.reg(kAddr, kPrescale) == 255);
    pwm.setPWMFreq(100000.0f);  // clamps to 3500 Hz
    BOOST_TEST(bus.reg(kAddr, kPrescale) == 3);  // clamped up to the minimum
}

BOOST_AUTO_TEST_CASE(write_microseconds_1500_at_50hz_is_307_ticks) {
    start_at_50hz();
    pwm.writeMicroseconds(0, 1500);
    BOOST_TEST(bus.reg(kAddr, 0x06) == 0x00);  // LED0_ON_L
    BOOST_TEST(bus.reg(kAddr, 0x07) == 0x00);  // LED0_ON_H
    BOOST_TEST(bus.reg(kAddr, 0x08) == 0x33);  // LED0_OFF_L: 307 = 0x133
    BOOST_TEST(bus.reg(kAddr, 0x09) == 0x01);  // LED0_OFF_H
}

BOOST_AUTO_TEST_CASE(each_channel_has_its_own_register_block) {
    start_at_50hz();
    pwm.writeMicroseconds(5, 1500);
    pwm.writeMicroseconds(15, 1500);
    BOOST_TEST(bus.reg(kAddr, 0x1A + 2) == 0x33);  // channel 5: base 0x1A
    BOOST_TEST(bus.reg(kAddr, 0x1A + 3) == 0x01);
    BOOST_TEST(bus.reg(kAddr, 0x42 + 2) == 0x33);  // channel 15: base 0x42
    BOOST_TEST(bus.reg(kAddr, 0x42 + 3) == 0x01);
    BOOST_TEST(bus.reg(kAddr, 0x06 + 2) == 0x00);  // channel 0 untouched
}

BOOST_AUTO_TEST_CASE(disable_pwm_sets_the_full_off_bit) {
    start_at_50hz();
    pwm.writeMicroseconds(2, 1500);
    BOOST_TEST(pwm.disablePWM(2));
    BOOST_TEST(bus.reg(kAddr, 0x0E + 2) == 0x00);  // OFF_L
    BOOST_TEST(bus.reg(kAddr, 0x0E + 3) == 0x10);  // OFF_H bit 4 = full off
}

BOOST_AUTO_TEST_CASE(set_pwm_and_get_pwm_round_trip_including_wraparound) {
    start_at_50hz();
    pwm.setPWM(3, 100, 400);
    BOOST_TEST(pwm.getPWM(3) == 300);
    pwm.setPWM(3, 3000, 500);
    BOOST_TEST(pwm.getPWM(3) == 1596);  // off < on wraps by 4096
}

BOOST_AUTO_TEST_CASE(set_pin_maps_the_extremes_to_full_on_and_full_off) {
    start_at_50hz();
    pwm.setPin(1, 4095);  // full on: ON bit 4
    BOOST_TEST(bus.reg(kAddr, 0x0A + 1) == 0x10);
    BOOST_TEST(bus.reg(kAddr, 0x0A + 3) == 0x00);
    pwm.setPin(1, 0);  // full off: OFF bit 4
    BOOST_TEST(bus.reg(kAddr, 0x0A + 3) == 0x10);
}

BOOST_AUTO_TEST_CASE(a_failed_read_reads_as_zero_like_before) {
    start_at_50hz();
    bus.fail_reads = true;
    BOOST_TEST(pwm.readPrescale() == 0);
}

BOOST_AUTO_TEST_SUITE_END()
