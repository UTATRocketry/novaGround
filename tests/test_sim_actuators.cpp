#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "actuators.hpp"
#include "controllers/relay_controller.hpp"
#include "controllers/servo_controller.hpp"
#include "core/telemetry.hpp"
#include "sim/daqhats_sim.hpp"
#include "sim/sim_actuators.hpp"
#include "temp_dir.hpp"

namespace json = boost::json;

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

// build_actuators from actuators_sim.cpp: the startup state of the real chips.
BOOST_AUTO_TEST_CASE(startup_turns_all_relays_on_and_the_output_pins_low) {
    daqhats_sim::load(parse_scenario(R"({"gpio_inputs":{"5":1}})"));
    TempDir dir;
    std::vector<std::string> actuator_headers{"timestamp", "gpio_17"};
    for (int i = 0; i < 16; ++i) actuator_headers.push_back("relay_" + std::to_string(i));
    for (int i = 0; i < 16; ++i) actuator_headers.push_back("servo_" + std::to_string(i));
    actuator_headers.push_back("type_id");
    DataLogger logger(dir.path().string(), {"timestamp"}, actuator_headers, {17}, "n");

    Actuators a = build_actuators(logger, {17}, {5});

    BOOST_REQUIRE(a.relays);
    BOOST_REQUIRE(a.servo);
    BOOST_REQUIRE(a.gpio);
    BOOST_TEST(a.relay_state.all());
    BOOST_TEST(a.servo->is_ready());

    int level = -1;
    BOOST_TEST(a.gpio->write(17, 1));  // 17 was set up as an output
    BOOST_TEST(a.gpio->read(5, level));
    BOOST_TEST(level == 1);            // from the scenario
    BOOST_TEST(!a.gpio->write(5, 1));  // 5 is an input

    // The logger was told the relays start on.
    BOOST_REQUIRE(logger.start("r"));
    logger.log_actuator_snapshot("startup");
    logger.stop();
    std::string text = read_file(logger.last_actuator_path());
    auto row = text.substr(text.find('\n') + 1);
    std::vector<std::string> f;
    std::istringstream in(row.substr(0, row.find('\n')));
    for (std::string x; std::getline(in, x, ',');) f.push_back(x);
    BOOST_REQUIRE_EQUAL(f.size(), 35u);
    for (int i = 0; i < 16; ++i) BOOST_TEST(f[2 + i] == "1", "relay " << i);
    daqhats_sim::reset();
}

BOOST_AUTO_TEST_CASE(the_real_controllers_drive_the_simulated_outputs) {
    daqhats_sim::reset();
    TempDir dir;
    DataLogger logger(dir.path().string(), {"timestamp"}, {"timestamp"}, {}, "n");
    Actuators a = build_actuators(logger, {17}, {5});
    TelemetryStore telemetry;

    RelayController relays(a.relays.get(), &telemetry, nullptr);
    relays.handle_command(json::parse(R"({"id":3,"state":false})").as_object());
    BOOST_TEST(telemetry.snapshot_relay_state().test(3) == false);

    ServoController servos(a.servo.get(), &telemetry, nullptr);
    servos.handle_command(json::parse(R"({"id":2,"angle":1700})").as_object());
    auto states = telemetry.snapshot_servo_states();
    BOOST_REQUIRE_EQUAL(states.size(), 1u);
    BOOST_TEST(states[0].angle == 1700);
    BOOST_TEST(states[0].enabled);
    daqhats_sim::reset();
}

BOOST_AUTO_TEST_SUITE_END()
