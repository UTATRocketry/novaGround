#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "core/pi_io_config.hpp"

namespace {
const std::set<int> kMcc134 = {1};

ParsedIoConfig parse(const std::string& text) {
    return parse_pi_io_config(text, kMcc134);
}

// The reason given for `item`, or "" if it was not rejected.
std::string reason_for(const ParsedIoConfig& p, const std::string& item) {
    for (const auto& r : p.rejected) {
        if (r.item == item) return r.reason;
    }
    return "";
}

bool rejected(const ParsedIoConfig& p, const std::string& item) {
    return !reason_for(p, item).empty();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(pi_io_config)

BOOST_AUTO_TEST_CASE(the_design_docs_example_is_applied_in_full) {
    auto p = parse(R"({
      "config_hash": "3f9a1c0be27d4a55",
      "gpio": {
        "outputs": {
          "17": { "max_on_ms": 1800000, "on_comms_loss": "safe" },
          "27": { "max_on_ms": 600000, "on_comms_loss": "safe" }
        },
        "inputs": [5]
      },
      "relays": { "3": { "max_on_ms": null, "on_comms_loss": "hold" } },
      "daq": { "1": { "0": "K", "1": "T" } },
      "heartbeat": { "timeout_ms": 2000 }
    })");
    BOOST_TEST(p.usable);
    BOOST_TEST(p.rejected.empty());
    const auto& c = p.config;
    BOOST_TEST(c.config_hash.value() == "3f9a1c0be27d4a55");
    BOOST_REQUIRE_EQUAL(c.gpio_outputs.size(), 2u);
    BOOST_TEST(c.gpio_outputs.at(17).max_on_ms.value() == 1800000);
    BOOST_TEST(c.gpio_outputs.at(17).safe_on_comms_loss);
    BOOST_TEST(c.gpio_outputs.at(27).max_on_ms.value() == 600000);
    BOOST_TEST((c.gpio_inputs == std::set<int>{5}));
    BOOST_REQUIRE_EQUAL(c.relays.size(), 1u);
    BOOST_TEST(!c.relays.at(3).max_on_ms.has_value());
    BOOST_TEST(!c.relays.at(3).safe_on_comms_loss);
    BOOST_TEST((c.daq.at(1).at(0) == TcType::K));
    BOOST_TEST((c.daq.at(1).at(1) == TcType::T));
    BOOST_TEST(c.heartbeat_timeout_ms.value() == 2000);
}

BOOST_AUTO_TEST_CASE(limits_default_to_hold_with_no_max_on_time) {
    auto p = parse(R"({"gpio":{"outputs":{"22":{}}}})");
    BOOST_TEST(p.rejected.empty());
    BOOST_TEST(!p.config.gpio_outputs.at(22).max_on_ms.has_value());
    BOOST_TEST(!p.config.gpio_outputs.at(22).safe_on_comms_loss);
}

BOOST_AUTO_TEST_CASE(an_empty_object_is_a_config_with_nothing_in_it) {
    auto p = parse("{}");
    BOOST_TEST(p.usable);
    BOOST_TEST(p.rejected.empty());
    BOOST_TEST(!p.config.config_hash.has_value());
    BOOST_TEST(p.config.gpio_outputs.empty());
    BOOST_TEST(!p.config.heartbeat_timeout_ms.has_value());
}

BOOST_AUTO_TEST_CASE(a_message_that_is_not_a_json_object_is_not_usable) {
    for (const char* text : {"", "not json", "[1,2]", "42", "\"gpio\"", "{\"gpio\":"}) {
        auto p = parse(text);
        BOOST_TEST(!p.usable, text);
        BOOST_REQUIRE_EQUAL(p.rejected.size(), 1u);
        BOOST_TEST(p.rejected[0].item == "(message)");
    }
}

// ── reserved pins and the BCM range ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(the_reserved_list_is_the_design_docs) {
    std::vector<int> pins;
    for (const auto& r : reserved_pins()) pins.push_back(r.pin);
    BOOST_TEST((pins == std::vector<int>{0, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 16, 20, 21, 26}));
    BOOST_TEST(std::string(reserved_pin_use(8)) == "SPI0 CE0");
    BOOST_TEST(reserved_pin_use(17) == nullptr);
}

BOOST_AUTO_TEST_CASE(every_reserved_pin_is_rejected_as_an_output_and_as_an_input) {
    for (const auto& r : reserved_pins()) {
        const std::string pin = std::to_string(r.pin);
        auto p = parse(R"({"gpio":{"outputs":{")" + pin + R"(":{}},"inputs":[)" + pin + "]}}");
        BOOST_TEST(p.config.gpio_outputs.empty(), pin);
        BOOST_TEST(p.config.gpio_inputs.empty(), pin);
        BOOST_TEST(reason_for(p, "gpio.outputs." + pin) == std::string("reserved (") + r.use + ")");
        BOOST_TEST(reason_for(p, "gpio.inputs." + pin) == std::string("reserved (") + r.use + ")");
    }
}

BOOST_AUTO_TEST_CASE(the_design_docs_rejection_example) {
    auto p = parse(R"({"gpio":{"outputs":{"8":{}}}})");
    BOOST_REQUIRE_EQUAL(p.rejected.size(), 1u);
    BOOST_TEST(p.rejected[0].item == "gpio.outputs.8");
    BOOST_TEST(p.rejected[0].reason == "reserved (SPI0 CE0)");
}

BOOST_AUTO_TEST_CASE(every_free_pin_is_accepted) {
    for (int pin : {4, 5, 6, 14, 15, 17, 18, 19, 22, 23, 24, 25, 27}) {
        auto p = parse(R"({"gpio":{"outputs":{")" + std::to_string(pin) + R"(":{}}}})");
        BOOST_TEST(p.rejected.empty(), pin);
        BOOST_TEST(p.config.gpio_outputs.count(pin) == 1u, pin);
    }
}

BOOST_AUTO_TEST_CASE(pins_outside_bcm_0_to_27_are_rejected) {
    auto p = parse(R"({"gpio":{"outputs":{"28":{},"40":{},"99999":{}},"inputs":[28,-1,4294967296]}})");
    BOOST_TEST(p.config.gpio_outputs.empty());
    BOOST_TEST(p.config.gpio_inputs.empty());
    BOOST_TEST(reason_for(p, "gpio.outputs.28") == "outside BCM 0-27");
    BOOST_TEST(reason_for(p, "gpio.outputs.40") == "outside BCM 0-27");
    BOOST_TEST(rejected(p, "gpio.outputs.99999"));
    BOOST_TEST(reason_for(p, "gpio.inputs.28") == "outside BCM 0-27");
    BOOST_TEST(reason_for(p, "gpio.inputs.-1") == "outside BCM 0-27");
    BOOST_TEST(reason_for(p, "gpio.inputs.4294967296") == "outside BCM 0-27");
}

BOOST_AUTO_TEST_CASE(pin_keys_must_be_plain_decimal_numbers) {
    auto p = parse(R"({"gpio":{"outputs":{"017":{},"17a":{},"":{}," 17":{},"-17":{},"GPIO17":{}}}})");
    BOOST_TEST(p.config.gpio_outputs.empty());
    BOOST_TEST(p.rejected.size() == 6u);
    BOOST_TEST(reason_for(p, "gpio.outputs.017") == "not a pin number");
}

BOOST_AUTO_TEST_CASE(inputs_must_be_whole_numbers) {
    auto p = parse(R"({"gpio":{"inputs":[5,"6",6.5,true,null,{}]}})");
    BOOST_TEST((p.config.gpio_inputs == std::set<int>{5}));
    BOOST_TEST(reason_for(p, "gpio.inputs[1]") == "not a pin number");
    BOOST_TEST(rejected(p, "gpio.inputs[2]"));
    BOOST_TEST(rejected(p, "gpio.inputs[3]"));
    BOOST_TEST(rejected(p, "gpio.inputs[4]"));
    BOOST_TEST(rejected(p, "gpio.inputs[5]"));
}

// ── duplicates ───────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(a_pin_listed_as_both_input_and_output_is_neither) {
    auto p = parse(R"({"gpio":{"outputs":{"17":{},"27":{}},"inputs":[17,5]}})");
    BOOST_TEST((p.config.gpio_outputs.count(17) == 0u));
    BOOST_TEST((p.config.gpio_inputs == std::set<int>{5}));
    BOOST_TEST(p.config.gpio_outputs.count(27) == 1u);
    BOOST_TEST(reason_for(p, "gpio.outputs.17") == "listed as both input and output");
    BOOST_TEST(reason_for(p, "gpio.inputs.17") == "listed as both input and output");
}

BOOST_AUTO_TEST_CASE(an_input_listed_twice_is_kept_once_and_the_repeat_rejected) {
    auto p = parse(R"({"gpio":{"inputs":[5,6,5]}})");
    BOOST_TEST((p.config.gpio_inputs == std::set<int>{5, 6}));
    BOOST_REQUIRE_EQUAL(p.rejected.size(), 1u);
    BOOST_TEST(p.rejected[0].item == "gpio.inputs.5");
    BOOST_TEST(p.rejected[0].reason == "listed twice");
}

// ── malformed entries ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(unknown_keys_are_rejected_at_every_level_and_the_rest_applied) {
    auto p = parse(R"({
      "config_hash": "abc",
      "colour": "red",
      "gpio": { "outputs": { "17": {"max_on_ms": 1000, "polarity": "low"}, "27": {} },
                "pwm": [] },
      "heartbeat": { "timeout_ms": 2000, "interval_ms": 250 }
    })");
    BOOST_TEST(reason_for(p, "colour") == "unknown key");
    BOOST_TEST(reason_for(p, "gpio.pwm") == "unknown key");
    BOOST_TEST(reason_for(p, "gpio.outputs.17") == "unknown key 'polarity'");
    BOOST_TEST(reason_for(p, "heartbeat.interval_ms") == "unknown key");
    BOOST_TEST(p.config.gpio_outputs.count(17) == 0u);
    BOOST_TEST(p.config.gpio_outputs.count(27) == 1u);
    BOOST_TEST(p.config.heartbeat_timeout_ms.value() == 2000);
    BOOST_TEST(p.config.config_hash.value() == "abc");
}

BOOST_AUTO_TEST_CASE(bad_limits_reject_that_output_only) {
    auto p = parse(R"({"gpio":{"outputs":{
        "4":  {"max_on_ms": 0},
        "5":  {"max_on_ms": -5},
        "6":  {"max_on_ms": 1.5},
        "14": {"max_on_ms": "600000"},
        "15": {"on_comms_loss": "off"},
        "17": {"on_comms_loss": true},
        "18": 7,
        "27": {"max_on_ms": 600000, "on_comms_loss": "safe"}}}})");
    for (const char* item : {"gpio.outputs.4", "gpio.outputs.5", "gpio.outputs.6", "gpio.outputs.14",
                             "gpio.outputs.15", "gpio.outputs.17", "gpio.outputs.18"}) {
        BOOST_TEST(rejected(p, item), item);
    }
    BOOST_REQUIRE_EQUAL(p.config.gpio_outputs.size(), 1u);
    BOOST_TEST(p.config.gpio_outputs.at(27).safe_on_comms_loss);
}

BOOST_AUTO_TEST_CASE(sections_of_the_wrong_type_are_rejected) {
    auto p = parse(R"({"config_hash":5,"gpio":[],"relays":"3","daq":1,"heartbeat":2000})");
    BOOST_TEST(p.usable);
    for (const char* item : {"config_hash", "gpio", "relays", "daq", "heartbeat"}) {
        BOOST_TEST(rejected(p, item), item);
    }
    BOOST_TEST(!p.config.config_hash.has_value());
    BOOST_TEST(!p.config.heartbeat_timeout_ms.has_value());
}

BOOST_AUTO_TEST_CASE(relay_channels_are_0_to_15) {
    auto p = parse(R"({"relays":{"0":{},"15":{"on_comms_loss":"safe"},"16":{},"x":{}}})");
    BOOST_TEST(p.config.relays.size() == 2u);
    BOOST_TEST(p.config.relays.at(15).safe_on_comms_loss);
    BOOST_TEST(rejected(p, "relays.16"));
    BOOST_TEST(rejected(p, "relays.x"));
}

BOOST_AUTO_TEST_CASE(the_heartbeat_needs_a_positive_timeout) {
    BOOST_TEST(!parse(R"({"heartbeat":{}})").config.heartbeat_timeout_ms.has_value());
    BOOST_TEST(rejected(parse(R"({"heartbeat":{}})"), "heartbeat.timeout_ms"));
    BOOST_TEST(rejected(parse(R"({"heartbeat":{"timeout_ms":0}})"), "heartbeat.timeout_ms"));
    BOOST_TEST(rejected(parse(R"({"heartbeat":{"timeout_ms":"2000"}})"), "heartbeat.timeout_ms"));
    BOOST_TEST(parse(R"({"heartbeat":{"timeout_ms":500}})").config.heartbeat_timeout_ms.value() == 500);
}

// ── thermocouples ────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(every_library_type_is_accepted) {
    auto p = parse(R"({"daq":{"1":{"0":"J","1":"E","2":"R","3":"S"}}})");
    BOOST_TEST(p.rejected.empty());
    BOOST_TEST((p.config.daq.at(1).at(0) == TcType::J));
    BOOST_TEST((p.config.daq.at(1).at(3) == TcType::S));
    auto q = parse(R"({"daq":{"1":{"0":"B","1":"N","2":"T","3":"K"}}})");
    BOOST_TEST(q.rejected.empty());
}

BOOST_AUTO_TEST_CASE(unknown_types_channels_and_addresses_are_rejected) {
    auto p = parse(R"({"daq":{
        "1": {"0": "k", "1": "X", "2": 1, "4": "K", "3": "T"},
        "0": {"0": "K"},
        "9": {"0": "K"},
        "a": {"0": "K"}}})");
    BOOST_TEST(rejected(p, "daq.1.0"));  // lower case
    BOOST_TEST(rejected(p, "daq.1.1"));
    BOOST_TEST(rejected(p, "daq.1.2"));
    BOOST_TEST(reason_for(p, "daq.1.4") == "not an MCC 134 channel (0-3)");
    BOOST_TEST(reason_for(p, "daq.0") == "no MCC 134 at this address");
    BOOST_TEST(reason_for(p, "daq.9") == "no MCC 134 at this address");
    BOOST_TEST(rejected(p, "daq.a"));
    BOOST_REQUIRE_EQUAL(p.config.daq.size(), 1u);
    BOOST_TEST(p.config.daq.at(1).size() == 1u);
    BOOST_TEST((p.config.daq.at(1).at(3) == TcType::T));
}

BOOST_AUTO_TEST_CASE(tc_type_names_round_trip) {
    for (const char* name : {"J", "K", "T", "E", "R", "S", "B", "N"}) {
        auto type = parse_tc_type(name);
        BOOST_REQUIRE(type.has_value());
        BOOST_TEST(std::string(tc_type_name(*type)) == name);
    }
    BOOST_TEST(!parse_tc_type("disabled").has_value());
    BOOST_TEST(!parse_tc_type("KK").has_value());
    BOOST_TEST((kUnconfiguredTcType == TcType::K));
}

BOOST_AUTO_TEST_SUITE_END()
