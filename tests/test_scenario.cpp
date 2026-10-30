#include <boost/test/unit_test.hpp>

#include <fstream>

#include "sim/scenario.hpp"
#include "temp_dir.hpp"

namespace {
const char* kFull = R"({
  "seed": 5,
  "hats": [
    { "address": 0, "type": "mcc128",
      "channels": {
        "0": { "wave": "sine", "min": 1, "max": 5, "period": 20, "noise": 0.02 },
        "3": { "wave": "flat", "value": 2.5 }
      } },
    { "address": 1, "type": "mcc134", "channels": { "0": { "wave": "square", "min": 10, "max": 20 } } }
  ],
  "faults": {
    "read_errors": [ { "address": 0, "channel": 3 } ],
    "missing_hats": [ 2 ],
    "read_delay_us": 250
  },
  "gpio_inputs": { "5": 1, "6": 0 }
})";
}

BOOST_AUTO_TEST_SUITE(scenario)

BOOST_AUTO_TEST_CASE(a_full_scenario_parses) {
    Scenario s = parse_scenario(kFull);
    BOOST_TEST(s.seed == 5u);
    BOOST_REQUIRE_EQUAL(s.hats.size(), 2u);
    BOOST_TEST(s.hats[0].address == 0);
    BOOST_TEST(s.hats[0].type == "mcc128");
    BOOST_TEST(s.hats[0].channels.at(0).wave == "sine");
    BOOST_TEST(s.hats[0].channels.at(0).min == 1.0);
    BOOST_TEST(s.hats[0].channels.at(0).max == 5.0);
    BOOST_TEST(s.hats[0].channels.at(0).period == 20.0);
    BOOST_TEST(s.hats[0].channels.at(0).noise == 0.02);
    BOOST_TEST(s.hats[0].channels.at(3).value == 2.5);
    BOOST_TEST(s.hats[1].type == "mcc134");
    BOOST_TEST((s.read_errors.count({0, 3}) == 1u));
    BOOST_TEST((s.missing_hats.count(2) == 1u));
    BOOST_TEST(s.read_delay_us == 250);
    BOOST_TEST(s.gpio_inputs.at(5) == 1);
    BOOST_TEST(s.gpio_inputs.at(6) == 0);
}

BOOST_AUTO_TEST_CASE(everything_but_the_hats_is_optional) {
    Scenario s = parse_scenario(R"({"hats":[{"address":3,"type":"mcc128"}]})");
    BOOST_TEST(s.seed == 1u);
    BOOST_REQUIRE_EQUAL(s.hats.size(), 1u);
    BOOST_TEST(s.hats[0].channels.empty());
    BOOST_TEST(s.read_errors.empty());
    BOOST_TEST(s.missing_hats.empty());
    BOOST_TEST(s.read_delay_us == 0);
    BOOST_TEST(s.gpio_inputs.empty());
    BOOST_TEST(parse_scenario("{}").hats.empty());
}

BOOST_AUTO_TEST_CASE(a_flat_channel_without_a_value_sits_at_the_midpoint) {
    Scenario s = parse_scenario(R"({"hats":[{"address":0,"type":"mcc128",
        "channels":{"0":{"wave":"flat","min":2,"max":4}}}]})");
    BOOST_TEST(s.hats[0].channels.at(0).value == 3.0);
}

BOOST_AUTO_TEST_CASE(bad_scenarios_are_rejected_with_a_reason) {
    BOOST_CHECK_THROW(parse_scenario("not json"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario("[]"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"hats":[{"type":"mcc128"}]})"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"hats":[{"address":0}]})"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"hats":[{"address":0,"type":"mcc999"}]})"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"hats":[{"address":"0","type":"mcc128"}]})"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"hats":[{"address":0,"type":"mcc128","channels":{"x":{}}}]})"),
                      ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"hats":[{"address":0,"type":"mcc128","channels":{"0":{"min":"low"}}}]})"),
                      ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"faults":{"read_errors":[{"address":0}]}})"), ScenarioError);
    BOOST_CHECK_THROW(parse_scenario(R"({"gpio_inputs":{"five":1}})"), ScenarioError);
}

BOOST_AUTO_TEST_CASE(the_error_message_names_the_problem) {
    try {
        parse_scenario(R"({"hats":[{"address":0,"type":"mcc999"}]})");
        BOOST_FAIL("expected a ScenarioError");
    } catch (const ScenarioError& e) {
        BOOST_TEST(std::string(e.what()).find("mcc999") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(a_scenario_file_loads_and_a_missing_one_is_an_error) {
    TempDir dir;
    auto path = (dir.path() / "scenario.json").string();
    { std::ofstream(path) << kFull; }
    BOOST_TEST(load_scenario_file(path).hats.size() == 2u);
    BOOST_CHECK_THROW(load_scenario_file((dir.path() / "nope.json").string()), ScenarioError);
}

BOOST_AUTO_TEST_CASE(the_default_scenario_has_the_python_simulators_gcs_channels) {
    Scenario s = default_scenario();
    BOOST_REQUIRE_EQUAL(s.hats.size(), 2u);
    BOOST_TEST(s.hats[0].address == 0);
    for (int channel : {0, 1, 4, 6, 7}) {
        BOOST_TEST(s.hats[0].channels.count(channel) == 1u, "channel " << channel);
    }
    BOOST_TEST(s.hats[1].address == 1);
    BOOST_TEST(s.hats[1].channels.count(1) == 1u);
    BOOST_TEST(s.hats[0].channels.at(0).wave == "sine");
}

BOOST_AUTO_TEST_SUITE_END()
