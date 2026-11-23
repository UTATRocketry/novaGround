#include <boost/test/unit_test.hpp>
#include <boost/test/tools/floating_point_comparison.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "controllers/sampler.hpp"
#include "daqhats/daqhats.h"
#include "daqhats/mcc128.h"
#include "daqhats/mcc134.h"
#include "drivers/mcc_daqhats.hpp"
#include "sim/daqhats_sim.hpp"
#include "temp_dir.hpp"

namespace tt = boost::test_tools;

namespace {
// Hat 0: MCC 128 with a sine on channel 0, a flat 2.5 on channel 3 and a read
// error on channel 5. Hat 1: MCC 134 with a flat 21.5 on channel 0. Hat 2 is
// in the file but missing from the bus.
const char* kScenario = R"({
  "hats": [
    { "address": 0, "type": "mcc128", "channels": {
        "0": { "wave": "sine", "min": 1, "max": 5, "period": 20 },
        "3": { "wave": "flat", "value": 2.5 } } },
    { "address": 1, "type": "mcc134", "channels": { "0": { "wave": "flat", "value": 21.5 } } },
    { "address": 2, "type": "mcc128" }
  ],
  "faults": { "read_errors": [ { "address": 0, "channel": 5 } ], "missing_hats": [ 2 ] },
  "gpio_inputs": { "5": 1 }
})";

struct Fixture {
    Fixture() {
        daqhats_sim::load(parse_scenario(kScenario));
        daqhats_sim::set_clock([] { return 5.0; });  // a quarter of the sine's period
    }
    ~Fixture() {
        daqhats_sim::reset();
        unsetenv("NOVA_SIM_SCENARIO");
    }
};
}

BOOST_FIXTURE_TEST_SUITE(daqhats_sim_api, Fixture)

BOOST_AUTO_TEST_CASE(hat_list_counts_the_hats_that_are_on_the_bus) {
    BOOST_TEST(hat_list(HAT_ID_ANY, nullptr) == 2);  // the missing hat is not found
    HatInfo list[2];
    BOOST_REQUIRE_EQUAL(hat_list(HAT_ID_ANY, list), 2);
    BOOST_TEST(list[0].address == 0);
    BOOST_TEST(list[0].id == HAT_ID_MCC_128);
    BOOST_TEST(list[1].address == 1);
    BOOST_TEST(list[1].id == HAT_ID_MCC_134);
}

BOOST_AUTO_TEST_CASE(hat_list_can_filter_by_product_id) {
    BOOST_TEST(hat_list(HAT_ID_MCC_128, nullptr) == 1);
    BOOST_TEST(hat_list(HAT_ID_MCC_134, nullptr) == 1);
    BOOST_TEST(hat_list(0x7FFF, nullptr) == 0);
}

BOOST_AUTO_TEST_CASE(open_succeeds_only_for_a_present_hat_of_the_right_type) {
    BOOST_TEST(mcc128_open(0) == RESULT_SUCCESS);
    BOOST_TEST(mcc134_open(1) == RESULT_SUCCESS);
    BOOST_TEST(mcc134_open(0) == RESULT_INVALID_DEVICE);  // an MCC 128
    BOOST_TEST(mcc128_open(1) == RESULT_INVALID_DEVICE);  // an MCC 134
    BOOST_TEST(mcc128_open(2) == RESULT_INVALID_DEVICE);  // missing
    BOOST_TEST(mcc128_open(7) == RESULT_INVALID_DEVICE);  // not in the file
}

BOOST_AUTO_TEST_CASE(reading_needs_an_open_hat) {
    double v = -1;
    BOOST_TEST(mcc128_a_in_read(0, 3, OPTS_DEFAULT, &v) == RESULT_INVALID_DEVICE);
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    BOOST_TEST(mcc128_a_in_read(0, 3, OPTS_DEFAULT, &v) == RESULT_SUCCESS);
    BOOST_TEST(v == 2.5);
}

BOOST_AUTO_TEST_CASE(a_read_follows_the_waveform_at_the_simulation_clock) {
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    double v = 0;
    BOOST_REQUIRE_EQUAL(mcc128_a_in_read(0, 0, OPTS_DEFAULT, &v), RESULT_SUCCESS);
    BOOST_TEST(v == 5.0, tt::tolerance(1e-9));  // sine peak at t = period / 4
    daqhats_sim::set_clock([] { return 0.0; });
    BOOST_REQUIRE_EQUAL(mcc128_a_in_read(0, 0, OPTS_DEFAULT, &v), RESULT_SUCCESS);
    BOOST_TEST(v == 3.0, tt::tolerance(1e-9));  // midpoint
}

BOOST_AUTO_TEST_CASE(a_channel_with_no_waveform_reads_zero) {
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    double v = -1;
    BOOST_TEST(mcc128_a_in_read(0, 7, OPTS_DEFAULT, &v) == RESULT_SUCCESS);
    BOOST_TEST(v == 0.0);
}

BOOST_AUTO_TEST_CASE(bad_arguments_are_bad_parameters) {
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    double v = 0;
    BOOST_TEST(mcc128_a_in_read(0, 8, OPTS_DEFAULT, &v) == RESULT_BAD_PARAMETER);  // only 0-7
    BOOST_TEST(mcc128_a_in_read(0, 0, OPTS_DEFAULT, nullptr) == RESULT_BAD_PARAMETER);
    BOOST_REQUIRE_EQUAL(mcc134_open(1), RESULT_SUCCESS);
    BOOST_TEST(mcc134_t_in_read(1, 4, &v) == RESULT_BAD_PARAMETER);  // only 0-3
}

BOOST_AUTO_TEST_CASE(a_configured_read_error_is_a_comms_failure_on_that_channel_only) {
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    double v = 0;
    BOOST_TEST(mcc128_a_in_read(0, 5, OPTS_DEFAULT, &v) == RESULT_COMMS_FAILURE);
    BOOST_TEST(mcc128_a_in_read(0, 4, OPTS_DEFAULT, &v) == RESULT_SUCCESS);
}

BOOST_AUTO_TEST_CASE(the_mcc134_sets_a_thermocouple_type_only_when_open) {
    BOOST_TEST(mcc134_tc_type_write(1, 0, TC_TYPE_K) == RESULT_INVALID_DEVICE);
    BOOST_REQUIRE_EQUAL(mcc134_open(1), RESULT_SUCCESS);
    BOOST_TEST(mcc134_tc_type_write(1, 0, TC_TYPE_K) == RESULT_SUCCESS);
    BOOST_TEST(mcc134_tc_type_write(1, 4, TC_TYPE_K) == RESULT_BAD_PARAMETER);
    double v = 0;
    BOOST_TEST(mcc134_t_in_read(1, 0, &v) == RESULT_SUCCESS);
    BOOST_TEST(v == 21.5);
}

BOOST_AUTO_TEST_CASE(a_read_delay_slows_reads_down) {
    Scenario s = parse_scenario(
        R"({"hats":[{"address":0,"type":"mcc128"}],"faults":{"read_delay_us":20000}})");
    daqhats_sim::load(s);
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    double v = 0;
    auto start = std::chrono::steady_clock::now();
    BOOST_REQUIRE_EQUAL(mcc128_a_in_read(0, 0, OPTS_DEFAULT, &v), RESULT_SUCCESS);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start).count();
    BOOST_TEST(ms >= 15);
}

BOOST_AUTO_TEST_CASE(loading_a_scenario_forgets_which_hats_were_open) {
    BOOST_REQUIRE_EQUAL(mcc128_open(0), RESULT_SUCCESS);
    daqhats_sim::load(parse_scenario(kScenario));
    double v = 0;
    BOOST_TEST(mcc128_a_in_read(0, 3, OPTS_DEFAULT, &v) == RESULT_INVALID_DEVICE);
}

BOOST_AUTO_TEST_CASE(gpio_inputs_come_from_the_scenario) {
    auto inputs = daqhats_sim::gpio_inputs();
    BOOST_REQUIRE_EQUAL(inputs.size(), 1u);
    BOOST_TEST(inputs.at(5) == 1);
}

BOOST_AUTO_TEST_CASE(without_a_scenario_variable_the_default_scenario_is_used) {
    daqhats_sim::reset();
    unsetenv("NOVA_SIM_SCENARIO");
    BOOST_TEST(hat_list(HAT_ID_ANY, nullptr) == 2);
}

BOOST_AUTO_TEST_CASE(the_scenario_variable_names_the_scenario_file) {
    TempDir dir;
    auto path = (dir.path() / "s.json").string();
    { std::ofstream(path) << R"({"hats":[{"address":4,"type":"mcc134"}]})"; }
    daqhats_sim::reset();
    setenv("NOVA_SIM_SCENARIO", path.c_str(), 1);
    HatInfo list[1];
    BOOST_REQUIRE_EQUAL(hat_list(HAT_ID_ANY, list), 1);
    BOOST_TEST(list[0].address == 4);
}

BOOST_AUTO_TEST_CASE(an_unreadable_scenario_file_fails_loudly_instead_of_falling_back) {
    daqhats_sim::reset();
    setenv("NOVA_SIM_SCENARIO", "/definitely/not/here.json", 1);
    BOOST_TEST(hat_list(HAT_ID_ANY, nullptr) < 0);
    BOOST_TEST(mcc128_open(0) < 0);
    BOOST_CHECK_THROW(MccDaqHats().discover(), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

// The real MccDaqHats driver (compiled against these simulated headers) over the
// simulated library.
BOOST_FIXTURE_TEST_SUITE(mcc_driver_over_the_sim, Fixture)

BOOST_AUTO_TEST_CASE(discover_lists_the_present_hats_with_their_channels) {
    MccDaqHats daq;
    auto hats = daq.discover();
    BOOST_REQUIRE_EQUAL(hats.size(), 2u);
    BOOST_TEST(hats[0].address == 0);
    BOOST_TEST(hats[0].hat_id == HAT_ID_MCC_128);
    BOOST_TEST((hats[0].channels == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    BOOST_TEST(hats[1].address == 1);
    BOOST_TEST(hats[1].hat_id == HAT_ID_MCC_134);
    BOOST_TEST((hats[1].channels == std::vector<int>{0, 1, 2, 3}));
}

BOOST_AUTO_TEST_CASE(open_and_read_go_through_to_the_library) {
    MccDaqHats daq;
    auto hats = daq.discover();
    for (const auto& hat : hats) BOOST_REQUIRE(daq.open(hat));

    double v = 0;
    BOOST_TEST(daq.read(hats[0], 0, v));
    BOOST_TEST(v == 5.0, tt::tolerance(1e-9));
    BOOST_TEST(daq.read(hats[1], 0, v));
    BOOST_TEST(v == 21.5);
    BOOST_TEST(!daq.read(hats[0], 5, v));  // the configured read error
}

BOOST_AUTO_TEST_CASE(reading_a_hat_that_was_not_opened_fails) {
    MccDaqHats daq;
    auto hats = daq.discover();
    double v = 0;
    BOOST_TEST(!daq.read(hats[0], 3, v));
}

BOOST_AUTO_TEST_CASE(an_unknown_hat_type_cannot_be_opened_or_read) {
    MccDaqHats daq;
    DaqHatDevice odd;
    odd.address = 0;
    odd.hat_id = 0x0999;
    double v = 0;
    BOOST_TEST(!daq.open(odd));
    BOOST_TEST(!daq.read(odd, 0, v));
}

BOOST_AUTO_TEST_CASE(discover_and_open_opens_every_hat_that_can_be_opened) {
    MccDaqHats daq;
    auto hats = discover_and_open(daq);
    BOOST_TEST(hats.size() == 2u);
}

BOOST_AUTO_TEST_CASE(a_whole_sampling_pass_over_the_simulated_hats) {
    MccDaqHats daq;
    auto hats = discover_and_open(daq);
    TelemetryStore store;
    daq_sample_once(daq, hats, store, nullptr);

    auto samples = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(samples.size(), 12u);  // 8 + 4 channels
    BOOST_TEST(samples[0].hat_id == 0);
    BOOST_TEST(samples[0].value == 5.0, tt::tolerance(1e-9));
    BOOST_TEST(samples[3].value == 2.5);
    BOOST_TEST(std::isnan(samples[5].value));  // the configured read error
    BOOST_TEST(samples[8].hat_id == 1);
    BOOST_TEST(samples[8].value == 21.5);
}

BOOST_AUTO_TEST_SUITE_END()
