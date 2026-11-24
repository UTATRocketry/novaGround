#include <boost/test/unit_test.hpp>

#include <map>
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
    SimDigitalIo io(std::map<int, int>{{5, 1}});
    int v = -1;
    int w = -1;
    BOOST_TEST(!io.write(17, 1));  // not configured
    BOOST_TEST(!io.read(5, v));    // not configured

    BOOST_TEST(io.configure_output(17, 0));
    BOOST_TEST(io.configure_input(5));
    BOOST_TEST(io.configure_input(6));
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
    SimDigitalIo io(std::map<int, int>{{5, 1}});
    io.configure_input(5);
    io.configure_input(6);
    io.configure_output(17, 0);
    BOOST_TEST((io.get_input_pins() == std::vector<int>{5, 6}));
    auto all = io.read_all_inputs();
    BOOST_TEST(all.size() == 2u);
    BOOST_TEST(all.at(5) == 1);
    BOOST_TEST(all.at(6) == 0);
}

BOOST_AUTO_TEST_CASE(the_relay_bank_reads_back_what_it_holds) {
    SimRelayBank bank;
    std::bitset<16> state;
    BOOST_TEST(bank.read_output(state));
    BOOST_TEST(state.none());
    bank.write_output(std::bitset<16>(0x0F0F));
    BOOST_TEST(bank.read_output(state));
    BOOST_TEST(state.to_ulong() == 0x0F0Fu);
}

BOOST_AUTO_TEST_CASE(the_pwm_driver_reads_back_pulse_off_and_range) {
    SimPwmDriver pwm;
    PwmChannelReadback r;
    BOOST_TEST(pwm.read_channel(3, r));  // never written
    BOOST_TEST((r.output == PwmOutput::Off));
    pwm.writeMicroseconds(3, 1200);
    BOOST_TEST(pwm.read_channel(3, r));
    BOOST_TEST((r.output == PwmOutput::Pulse));
    BOOST_TEST(r.microseconds == 1200);
    pwm.disablePWM(3);
    BOOST_TEST(pwm.read_channel(3, r));
    BOOST_TEST((r.output == PwmOutput::Off));
    BOOST_TEST(r.microseconds == 0);
    BOOST_TEST(!pwm.read_channel(16, r));
}

BOOST_AUTO_TEST_CASE(gpio_output_listing_only_has_outputs_and_their_levels) {
    SimDigitalIo io(std::map<int, int>{{5, 1}});
    io.configure_input(5);
    io.configure_output(17, 0);
    io.configure_output(27, 0);
    io.write(27, 1);
    auto outputs = io.read_all_outputs();
    BOOST_TEST(outputs.size() == 2u);
    BOOST_TEST(outputs.at(17) == 0);  // configured, never written: low
    BOOST_TEST(outputs.at(27) == 1);
}

BOOST_AUTO_TEST_CASE(gpio_claims_set_the_level_at_once_and_release_gives_the_pin_back) {
    SimDigitalIo io;
    BOOST_TEST(io.configure_output(17, 1));
    BOOST_TEST(io.claimed(17));
    BOOST_TEST(io.output_level(17) == 1);
    // Claiming an output again only sets the level.
    BOOST_TEST(io.configure_output(17, 0));
    BOOST_TEST(io.output_level(17) == 0);
    BOOST_TEST(io.release(17));
    BOOST_TEST(!io.claimed(17));
    BOOST_TEST(!io.write(17, 1));
    BOOST_TEST(io.read_all_outputs().empty());
    BOOST_TEST(io.release(17));  // releasing an unclaimed pin is fine
}

BOOST_AUTO_TEST_CASE(gpio_an_input_claimed_as_an_output_changes_direction) {
    SimDigitalIo io(std::map<int, int>{{5, 1}});
    BOOST_TEST(io.configure_input(5));
    BOOST_TEST(io.configure_output(5, 0));
    int v = -1;
    BOOST_TEST(!io.read(5, v));
    BOOST_TEST(io.write(5, 1));
    BOOST_TEST(io.configure_input(5));
    BOOST_TEST(io.read(5, v));
    BOOST_TEST(v == 1);
}

BOOST_AUTO_TEST_CASE(gpio_injected_failures_leave_the_pin_unclaimed_or_unchanged) {
    SimDigitalIo io;
    io.fail_claims_of(22);
    BOOST_TEST(!io.configure_output(22, 0));
    BOOST_TEST(!io.claimed(22));
    BOOST_TEST(!io.configure_input(22));
    BOOST_TEST(!io.claimed(22));

    BOOST_TEST(io.configure_output(17, 0));
    io.fail_writes_to(17);
    BOOST_TEST(!io.write(17, 1));
    BOOST_TEST(io.output_level(17) == 0);
    io.fail_writes_to(17, false);
    BOOST_TEST(io.write(17, 1));
    BOOST_TEST(io.output_level(17) == 1);
}

BOOST_AUTO_TEST_SUITE_END()
