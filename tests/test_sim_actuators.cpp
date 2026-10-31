#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "sim/sim_actuators.hpp"

BOOST_AUTO_TEST_SUITE(sim_actuators)

BOOST_AUTO_TEST_CASE(the_relay_bank_remembers_what_was_written) {
    SimRelayBank bank;
    BOOST_TEST(bank.is_ready());
    BOOST_TEST(bank.state().none());
    std::bitset<16> s(0x8001);
    BOOST_TEST(bank.write_output(s));
    BOOST_TEST((bank.state() == s));
    BOOST_TEST(bank.writes() == 1);
}

BOOST_AUTO_TEST_CASE(the_pwm_driver_keeps_pulse_widths_and_off_is_zero) {
    SimPwmDriver pwm;
    BOOST_TEST(pwm.is_ready());
    BOOST_TEST(pwm.microseconds(3) == 0);
    pwm.writeMicroseconds(3, 1200);
    BOOST_TEST(pwm.microseconds(3) == 1200);
    BOOST_TEST(pwm.disablePWM(3));
    BOOST_TEST(pwm.microseconds(3) == 0);
}

BOOST_AUTO_TEST_CASE(gpio_follows_the_real_drivers_contract) {
    SimDigitalIo io({{5, 1}});
    int v = -1;
    int w = -1;
    BOOST_TEST(!io.write(17, 1));  // not configured
    BOOST_TEST(!io.read(5, v));    // not configured
    BOOST_TEST(!io.set_direction(17, "sideways"));

    BOOST_TEST(io.set_direction(17, "out"));
    BOOST_TEST(io.set_direction(5, "in"));
    BOOST_TEST(io.set_direction(6, "in"));
    BOOST_TEST(io.write(17, 1));
    BOOST_TEST(io.output_level(17) == 1);
    BOOST_TEST(!io.write(5, 1));   // an input cannot be written
    BOOST_TEST(!io.read(17, w));   // an output cannot be read
    BOOST_TEST(io.read(5, v));
    BOOST_TEST(v == 1);
    BOOST_TEST(io.read(6, w));
    BOOST_TEST(w == 0);  // no level given: low
}

BOOST_AUTO_TEST_CASE(gpio_input_listing_only_has_inputs) {
    SimDigitalIo io({{5, 1}});
    io.set_direction(5, "in");
    io.set_direction(6, "in");
    io.set_direction(17, "out");
    BOOST_TEST((io.get_input_pins() == std::vector<int>{5, 6}));
    auto all = io.read_all_inputs();
    BOOST_TEST(all.size() == 2u);
    BOOST_TEST(all.at(5) == 1);
    BOOST_TEST(all.at(6) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
