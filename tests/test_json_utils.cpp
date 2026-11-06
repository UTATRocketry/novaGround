#include <boost/test/unit_test.hpp>

#include "utils/json_utils.hpp"

namespace json = boost::json;

static json::object parse(const char* text) {
    return json::parse(text).as_object();
}

BOOST_AUTO_TEST_SUITE(json_utils)

BOOST_AUTO_TEST_CASE(to_lower_lowercases_ascii) {
    BOOST_TEST(to_lower("RELAY On") == "relay on");
    BOOST_TEST(to_lower("") == "");
}

BOOST_AUTO_TEST_CASE(get_int_accepts_numbers_and_bools) {
    auto obj = parse(R"({"a": 5, "b": -3, "c": 3.9, "d": true, "e": false})");
    BOOST_TEST(get_int(obj, "a").value() == 5);
    BOOST_TEST(get_int(obj, "b").value() == -3);
    BOOST_TEST(get_int(obj, "c").value() == 3);
    BOOST_TEST(get_int(obj, "d").value() == 1);
    BOOST_TEST(get_int(obj, "e").value() == 0);
}

BOOST_AUTO_TEST_CASE(get_int_rejects_missing_and_wrong_types) {
    auto obj = parse(R"({"s": "5", "n": null, "o": {}, "l": []})");
    BOOST_TEST(!get_int(obj, "missing").has_value());
    BOOST_TEST(!get_int(obj, "s").has_value());
    BOOST_TEST(!get_int(obj, "n").has_value());
    BOOST_TEST(!get_int(obj, "o").has_value());
    BOOST_TEST(!get_int(obj, "l").has_value());
}

BOOST_AUTO_TEST_CASE(get_int_int64_bounds) {
    auto obj = parse(R"({"max": 9223372036854775807, "min": -9223372036854775808})");
    BOOST_TEST(get_int(obj, "max").value() == INT64_MAX);
    BOOST_TEST(get_int(obj, "min").value() == INT64_MIN);
}

BOOST_AUTO_TEST_CASE(get_bool_accepts_bools_and_numbers) {
    auto obj = parse(R"({"t": true, "f": false, "one": 1, "zero": 0, "d": 0.5, "dz": 0.0})");
    BOOST_TEST(get_bool(obj, "t").value() == true);
    BOOST_TEST(get_bool(obj, "f").value() == false);
    BOOST_TEST(get_bool(obj, "one").value() == true);
    BOOST_TEST(get_bool(obj, "zero").value() == false);
    BOOST_TEST(get_bool(obj, "d").value() == true);
    BOOST_TEST(get_bool(obj, "dz").value() == false);
}

BOOST_AUTO_TEST_CASE(get_bool_rejects_strings_and_missing) {
    auto obj = parse(R"({"s": "true", "n": null})");
    BOOST_TEST(!get_bool(obj, "s").has_value());
    BOOST_TEST(!get_bool(obj, "n").has_value());
    BOOST_TEST(!get_bool(obj, "missing").has_value());
}

BOOST_AUTO_TEST_CASE(get_string_only_accepts_strings) {
    auto obj = parse(R"({"s": "on", "e": "", "i": 1, "b": true})");
    BOOST_TEST(get_string(obj, "s").value() == "on");
    BOOST_TEST(get_string(obj, "e").value() == "");
    BOOST_TEST(!get_string(obj, "i").has_value());
    BOOST_TEST(!get_string(obj, "b").has_value());
    BOOST_TEST(!get_string(obj, "missing").has_value());
}

BOOST_AUTO_TEST_SUITE_END()
