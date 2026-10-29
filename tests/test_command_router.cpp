#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "core/command_router.hpp"

namespace json = boost::json;

namespace {
struct Fixture {
    CommandRouter router{"novaOps"};
    std::vector<json::object> relay_calls;
    std::vector<json::object> servo_calls;

    Fixture() {
        router.register_handler("relay", [this](const json::object& c) { relay_calls.push_back(c); });
        router.register_handler("servo", [this](const json::object& c) { servo_calls.push_back(c); });
    }
};
}

BOOST_FIXTURE_TEST_SUITE(command_router, Fixture)

BOOST_AUTO_TEST_CASE(command_object_reaches_its_handler) {
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":3,"state":"on"}})");
    BOOST_REQUIRE_EQUAL(relay_calls.size(), 1u);
    BOOST_TEST(servo_calls.empty());
    BOOST_TEST(relay_calls[0].at("id").as_int64() == 3);
    BOOST_TEST(relay_calls[0].at("state").as_string() == "on");
}

BOOST_AUTO_TEST_CASE(command_string_form_merges_sibling_keys) {
    router.handle_message(R"({"source":"novaOps","command":"servo","id":2,"angle":90})");
    BOOST_REQUIRE_EQUAL(servo_calls.size(), 1u);
    BOOST_TEST(servo_calls[0].at("type").as_string() == "servo");
    BOOST_TEST(servo_calls[0].at("id").as_int64() == 2);
    BOOST_TEST(servo_calls[0].at("angle").as_int64() == 90);
    BOOST_TEST(!servo_calls[0].contains("source"));
    BOOST_TEST(!servo_calls[0].contains("command"));
}

BOOST_AUTO_TEST_CASE(top_level_type_form) {
    router.handle_message(R"({"source":"novaOps","type":"relay","id":1})");
    BOOST_REQUIRE_EQUAL(relay_calls.size(), 1u);
    BOOST_TEST(relay_calls[0].at("id").as_int64() == 1);
}

BOOST_AUTO_TEST_CASE(wrong_source_is_ignored) {
    router.handle_message(R"({"source":"someoneElse","command":{"type":"relay","id":3}})");
    BOOST_TEST(relay_calls.empty());
}

BOOST_AUTO_TEST_CASE(missing_or_non_string_source_is_ignored) {
    router.handle_message(R"({"command":{"type":"relay","id":3}})");
    router.handle_message(R"({"source":5,"command":{"type":"relay","id":3}})");
    BOOST_TEST(relay_calls.empty());
}

BOOST_AUTO_TEST_CASE(bad_payloads_are_ignored) {
    router.handle_message("not json");
    router.handle_message("");
    router.handle_message("[1,2,3]");
    router.handle_message("42");
    router.handle_message(R"({"source":"novaOps"})");
    router.handle_message(R"({"source":"novaOps","command":5})");
    router.handle_message(R"({"source":"novaOps","command":{"id":1}})");
    router.handle_message(R"({"source":"novaOps","command":{"type":7}})");
    BOOST_TEST(relay_calls.empty());
    BOOST_TEST(servo_calls.empty());
}

BOOST_AUTO_TEST_CASE(unknown_type_is_ignored) {
    router.handle_message(R"({"source":"novaOps","command":{"type":"warp","id":1}})");
    BOOST_TEST(relay_calls.empty());
    BOOST_TEST(servo_calls.empty());
}

BOOST_AUTO_TEST_CASE(throwing_handler_does_not_stop_the_router) {
    int later_calls = 0;
    router.register_handler("boom", [](const json::object&) { throw std::runtime_error("x"); });
    router.register_handler("boom2", [](const json::object&) { throw 42; });
    router.register_handler("ok", [&later_calls](const json::object&) { ++later_calls; });

    router.handle_message(R"({"source":"novaOps","command":{"type":"boom"}})");
    router.handle_message(R"({"source":"novaOps","command":{"type":"boom2"}})");
    router.handle_message(R"({"source":"novaOps","command":{"type":"ok"}})");
    BOOST_TEST(later_calls == 1);
}

BOOST_AUTO_TEST_CASE(registering_a_type_again_replaces_the_handler) {
    int replaced = 0;
    router.register_handler("relay", [&replaced](const json::object&) { ++replaced; });
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":1}})");
    BOOST_TEST(replaced == 1);
    BOOST_TEST(relay_calls.empty());
}

BOOST_AUTO_TEST_SUITE_END()
