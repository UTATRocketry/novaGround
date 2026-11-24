#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "core/command_router.hpp"
#include "core/stats.hpp"

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
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":3,"state":"on"}})", CommandOrigin::Node);
    BOOST_REQUIRE_EQUAL(relay_calls.size(), 1u);
    BOOST_TEST(servo_calls.empty());
    BOOST_TEST(relay_calls[0].at("id").as_int64() == 3);
    BOOST_TEST(relay_calls[0].at("state").as_string() == "on");
}

BOOST_AUTO_TEST_CASE(command_string_form_merges_sibling_keys) {
    router.handle_message(R"({"source":"novaOps","command":"servo","id":2,"angle":90})", CommandOrigin::Node);
    BOOST_REQUIRE_EQUAL(servo_calls.size(), 1u);
    BOOST_TEST(servo_calls[0].at("type").as_string() == "servo");
    BOOST_TEST(servo_calls[0].at("id").as_int64() == 2);
    BOOST_TEST(servo_calls[0].at("angle").as_int64() == 90);
    BOOST_TEST(!servo_calls[0].contains("source"));
    BOOST_TEST(!servo_calls[0].contains("command"));
}

BOOST_AUTO_TEST_CASE(top_level_type_form) {
    router.handle_message(R"({"source":"novaOps","type":"relay","id":1})", CommandOrigin::Node);
    BOOST_REQUIRE_EQUAL(relay_calls.size(), 1u);
    BOOST_TEST(relay_calls[0].at("id").as_int64() == 1);
}

BOOST_AUTO_TEST_CASE(wrong_source_is_ignored) {
    router.handle_message(R"({"source":"someoneElse","command":{"type":"relay","id":3}})", CommandOrigin::Node);
    BOOST_TEST(relay_calls.empty());
}

BOOST_AUTO_TEST_CASE(missing_or_non_string_source_is_ignored) {
    router.handle_message(R"({"command":{"type":"relay","id":3}})", CommandOrigin::Node);
    router.handle_message(R"({"source":5,"command":{"type":"relay","id":3}})", CommandOrigin::Node);
    BOOST_TEST(relay_calls.empty());
}

BOOST_AUTO_TEST_CASE(bad_payloads_are_ignored) {
    router.handle_message("not json", CommandOrigin::Node);
    router.handle_message("", CommandOrigin::Node);
    router.handle_message("[1,2,3]", CommandOrigin::Node);
    router.handle_message("42", CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps"})", CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps","command":5})", CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps","command":{"id":1}})", CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps","command":{"type":7}})", CommandOrigin::Node);
    BOOST_TEST(relay_calls.empty());
    BOOST_TEST(servo_calls.empty());
}

BOOST_AUTO_TEST_CASE(unknown_type_is_ignored) {
    router.handle_message(R"({"source":"novaOps","command":{"type":"warp","id":1}})", CommandOrigin::Node);
    BOOST_TEST(relay_calls.empty());
    BOOST_TEST(servo_calls.empty());
}

BOOST_AUTO_TEST_CASE(throwing_handler_does_not_stop_the_router) {
    int later_calls = 0;
    router.register_handler("boom", [](const json::object&) { throw std::runtime_error("x"); });
    router.register_handler("boom2", [](const json::object&) { throw 42; });
    router.register_handler("ok", [&later_calls](const json::object&) { ++later_calls; });

    router.handle_message(R"({"source":"novaOps","command":{"type":"boom"}})", CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps","command":{"type":"boom2"}})", CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps","command":{"type":"ok"}})", CommandOrigin::Node);
    BOOST_TEST(later_calls == 1);
}

BOOST_AUTO_TEST_CASE(registering_a_type_again_replaces_the_handler) {
    int replaced = 0;
    router.register_handler("relay", [&replaced](const json::object&) { ++replaced; });
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":1}})", CommandOrigin::Node);
    BOOST_TEST(replaced == 1);
    BOOST_TEST(relay_calls.empty());
}

BOOST_AUTO_TEST_CASE(a_node_only_command_on_the_broadcast_topic_is_refused) {
    int gpio_calls = 0;
    router.register_handler("gpio", [&gpio_calls](const json::object&) { ++gpio_calls; }, true);
    router.register_handler("relay", [this](const json::object& c) { relay_calls.push_back(c); }, true);
    router.handle_message(R"({"source":"novaOps","command":{"type":"gpio","id":17,"state":true}})",
                          CommandOrigin::Broadcast);
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":3,"state":0}})",
                          CommandOrigin::Broadcast);
    BOOST_TEST(gpio_calls == 0);
    BOOST_TEST(relay_calls.empty());

    // The same commands on the node's own topic go through.
    router.handle_message(R"({"source":"novaOps","command":{"type":"gpio","id":17,"state":true}})",
                          CommandOrigin::Node);
    router.handle_message(R"({"source":"novaOps","command":{"type":"relay","id":3,"state":0}})",
                          CommandOrigin::Node);
    BOOST_TEST(gpio_calls == 1);
    BOOST_TEST(relay_calls.size() == 1u);
}

BOOST_AUTO_TEST_CASE(a_broadcast_command_works_on_either_topic) {
    int data_file_calls = 0;
    router.register_handler("data_file", [&data_file_calls](const json::object&) { ++data_file_calls; });
    router.handle_message(R"({"source":"novaOps","command":{"type":"data_file","action":"stop_data_saving"}})",
                          CommandOrigin::Broadcast);
    router.handle_message(R"({"source":"novaOps","command":{"type":"data_file","action":"stop_data_saving"}})",
                          CommandOrigin::Node);
    BOOST_TEST(data_file_calls == 2);
}

BOOST_AUTO_TEST_CASE(refusals_are_counted_and_not_timed) {
    StatsIdentity id;
    id.node = "novaTest";
    id.build_id = "novaTest";
    StatsRegistry stats(id, true);
    router.set_stats(&stats);
    router.register_handler("servo", [this](const json::object& c) { servo_calls.push_back(c); }, true);
    router.handle_message(R"({"source":"novaOps","command":{"type":"servo","id":2,"angle":90}})",
                          CommandOrigin::Broadcast);
    auto msg = json::parse(stats.take_stats_json()).as_object();
    BOOST_TEST(msg.at("refused_commands").to_number<std::int64_t>() == 1);
    BOOST_TEST(msg.at("commands").as_object().empty());
    BOOST_TEST(servo_calls.empty());
}

BOOST_AUTO_TEST_SUITE_END()
