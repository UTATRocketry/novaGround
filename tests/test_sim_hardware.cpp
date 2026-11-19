#include <boost/test/unit_test.hpp>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "controllers/relay_controller.hpp"
#include "controllers/sampler.hpp"
#include "controllers/servo_controller.hpp"
#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/hardware.hpp"
#include "sim/daqhats_sim.hpp"
#include "temp_dir.hpp"

namespace json = boost::json;

namespace {
const char* kScenario = R"({
  "hats": [ { "address": 0, "type": "mcc128", "channels": { "3": { "wave": "flat", "value": 2.5 } } } ],
  "gpio_inputs": { "5": 1 }
})";

struct Fixture {
    Fixture() { daqhats_sim::load(parse_scenario(kScenario)); }
    ~Fixture() { daqhats_sim::reset(); }
};
}

// build_hardware from sim/hardware_sim.cpp: the simulated machine.
BOOST_FIXTURE_TEST_SUITE(sim_hardware, Fixture)

BOOST_AUTO_TEST_CASE(everything_is_present_and_in_its_startup_state) {
    Hardware hw = build_hardware({17}, {5});

    BOOST_REQUIRE(hw.daq);
    BOOST_REQUIRE(hw.servo);
    BOOST_REQUIRE(hw.relays);
    BOOST_REQUIRE(hw.gpio);
    BOOST_TEST(hw.servo->is_ready());
    BOOST_TEST(hw.relays->is_ready());
    BOOST_TEST(hw.relay_state.all());  // all relays on at startup
}

BOOST_AUTO_TEST_CASE(the_daq_hats_are_the_scenarios_and_opened) {
    Hardware hw = build_hardware({17}, {5});
    BOOST_REQUIRE_EQUAL(hw.daq_hats.size(), 1u);
    BOOST_TEST(hw.daq_hats[0].address == 0);

    TelemetryStore store;
    daq_sample_once(*hw.daq, hw.daq_hats, store, nullptr);
    auto samples = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(samples.size(), 8u);
    BOOST_TEST(samples[3].value == 2.5);
}

BOOST_AUTO_TEST_CASE(a_scenario_without_hats_gives_no_daq_hats_but_the_rest_still_works) {
    daqhats_sim::load(parse_scenario(R"({"gpio_inputs":{"5":1}})"));
    Hardware hw = build_hardware({17}, {5});
    BOOST_TEST(hw.daq_hats.empty());
    BOOST_TEST(static_cast<bool>(hw.relays));
    BOOST_TEST(static_cast<bool>(hw.gpio));
}

BOOST_AUTO_TEST_CASE(gpio_has_the_given_pins_as_outputs_and_inputs_with_scenario_levels) {
    Hardware hw = build_hardware({17, 27}, {5, 6});
    int level = -1;
    BOOST_TEST(hw.gpio->write(17, 1));
    BOOST_TEST(hw.gpio->write(27, 1));
    BOOST_TEST(!hw.gpio->write(5, 1));  // an input
    BOOST_TEST(hw.gpio->read(5, level));
    BOOST_TEST(level == 1);  // from the scenario
    BOOST_TEST(hw.gpio->read(6, level));
    BOOST_TEST(level == 0);  // not listed: low
}

BOOST_AUTO_TEST_CASE(the_real_controllers_drive_the_simulated_outputs) {
    Hardware hw = build_hardware({17}, {5});
    TelemetryStore telemetry;

    RelayController relays(hw.relays.get(), &telemetry, nullptr);
    relays.set_initial_state(hw.relay_state);
    relays.handle_command(json::parse(R"({"id":3,"state":false})").as_object());
    BOOST_TEST(telemetry.snapshot_relay_state().test(3) == false);
    BOOST_TEST(telemetry.snapshot_relay_state().test(4) == true);

    ServoController servos(hw.servo.get(), &telemetry, nullptr);
    servos.handle_command(json::parse(R"({"id":2,"angle":1700})").as_object());
    auto states = telemetry.snapshot_servo_states();
    BOOST_REQUIRE_EQUAL(states.size(), 1u);
    BOOST_TEST(states[0].angle == 1700);
    BOOST_TEST(states[0].enabled);
}

BOOST_AUTO_TEST_SUITE_END()
