#include <boost/test/unit_test.hpp>

#include <chrono>

#include "fake_i2c_bus.hpp"
#include "interfaces/io_expander.hpp"

namespace {
constexpr uint8_t kAddr = 0x20;
const auto kNoDelay = std::chrono::milliseconds(0);
}

BOOST_AUTO_TEST_SUITE(tca9535)

BOOST_AUTO_TEST_CASE(ready_when_the_bus_can_address_the_chip) {
    FakeI2cBus bus;
    TCA9535 chip(bus, kAddr, kNoDelay);
    BOOST_TEST(chip.is_ready());
    BOOST_REQUIRE_EQUAL(bus.attached.size(), 1u);
    BOOST_TEST(bus.attached[0] == kAddr);
}

BOOST_AUTO_TEST_CASE(not_ready_when_the_bus_is_not_open_or_cannot_address) {
    FakeI2cBus closed;
    closed.ready = false;
    TCA9535 a(closed, kAddr, kNoDelay);
    BOOST_TEST(!a.is_ready());
    BOOST_TEST(a.last_error() == "Failed to open I2C bus");

    FakeI2cBus no_addr;
    no_addr.attach_ok = false;
    TCA9535 b(no_addr, kAddr, kNoDelay);
    BOOST_TEST(!b.is_ready());
    BOOST_TEST(b.last_error() == "Failed to set I2C device address");
}

BOOST_AUTO_TEST_CASE(a_chip_that_is_not_ready_never_touches_the_bus) {
    FakeI2cBus bus;
    bus.attach_ok = false;
    TCA9535 chip(bus, kAddr, kNoDelay);
    BOOST_TEST(!chip.configure_port(0, 0x00));
    BOOST_TEST(!chip.write_output(std::bitset<16>(0xFFFF)));
    uint8_t v = 0;
    BOOST_TEST(!chip.read_input(0, v));
    BOOST_TEST(bus.writes.empty());
}

BOOST_AUTO_TEST_CASE(configure_port_writes_the_config_registers) {
    FakeI2cBus bus;
    TCA9535 chip(bus, kAddr, kNoDelay);
    BOOST_TEST(chip.configure_port(0, 0x00));
    BOOST_TEST(chip.configure_port(1, 0xF0));
    BOOST_REQUIRE_EQUAL(bus.writes.size(), 2u);
    BOOST_TEST(bus.writes[0].reg == 0x06);
    BOOST_TEST(bus.writes[0].value == 0x00);
    BOOST_TEST(bus.writes[1].reg == 0x07);
    BOOST_TEST(bus.writes[1].value == 0xF0);
    BOOST_TEST(bus.writes[0].address == kAddr);
}

BOOST_AUTO_TEST_CASE(write_output_splits_the_16_bits_over_the_two_output_ports) {
    FakeI2cBus bus;
    TCA9535 chip(bus, kAddr, kNoDelay);
    BOOST_TEST(chip.write_output(std::bitset<16>(0xA50F)));
    BOOST_REQUIRE_EQUAL(bus.writes.size(), 2u);
    BOOST_TEST(bus.writes[0].reg == 0x02);  // OUTPUT_PORT0: relays 0-7
    BOOST_TEST(bus.writes[0].value == 0x0F);
    BOOST_TEST(bus.writes[1].reg == 0x03);  // OUTPUT_PORT1: relays 8-15
    BOOST_TEST(bus.writes[1].value == 0xA5);
}

BOOST_AUTO_TEST_CASE(single_relay_bits_land_in_the_right_port_and_bit) {
    for (int i = 0; i < 16; ++i) {
        FakeI2cBus bus;
        TCA9535 chip(bus, kAddr, kNoDelay);
        std::bitset<16> s;
        s.set(i);
        BOOST_TEST(chip.write_output(s));
        BOOST_TEST(bus.reg(kAddr, 0x02) == (i < 8 ? 1 << i : 0), "relay " << i);
        BOOST_TEST(bus.reg(kAddr, 0x03) == (i >= 8 ? 1 << (i - 8) : 0), "relay " << i);
    }
}

BOOST_AUTO_TEST_CASE(a_failed_write_is_retried_and_then_succeeds) {
    FakeI2cBus bus;
    TCA9535 chip(bus, kAddr, kNoDelay);
    bus.fail_writes = 1;  // the first port write fails
    BOOST_TEST(chip.write_output(std::bitset<16>(0x0102)));
    BOOST_TEST(bus.reg(kAddr, 0x02) == 0x02);
    BOOST_TEST(bus.reg(kAddr, 0x03) == 0x01);
    BOOST_TEST(bus.writes.size() == 4u);  // 2 first try + 2 retry
}

BOOST_AUTO_TEST_CASE(write_output_gives_up_after_ten_retries) {
    FakeI2cBus bus;
    TCA9535 chip(bus, kAddr, kNoDelay);
    bus.fail_writes = 1000;
    BOOST_TEST(!chip.write_output(std::bitset<16>(0xFFFF)));
    BOOST_TEST(bus.writes.size() == 22u);  // 1 first try + 10 retries, 2 writes each
}

BOOST_AUTO_TEST_CASE(read_input_reads_the_input_ports) {
    FakeI2cBus bus;
    TCA9535 chip(bus, kAddr, kNoDelay);
    bus.registers[{kAddr, 0x00}] = 0x12;
    bus.registers[{kAddr, 0x01}] = 0x34;
    uint8_t v = 0;
    BOOST_TEST(chip.read_input(0, v));
    BOOST_TEST(v == 0x12);
    BOOST_TEST(chip.read_input(1, v));
    BOOST_TEST(v == 0x34);
    bus.fail_reads = true;
    BOOST_TEST(!chip.read_input(0, v));
}

BOOST_AUTO_TEST_SUITE_END()
