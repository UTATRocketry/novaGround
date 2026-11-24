#include <boost/test/unit_test.hpp>

#include <string>
#include <utility>
#include <vector>

#include "core/command_router.hpp"
#include "core/node_topics.hpp"

namespace json = boost::json;

namespace {
struct Fixture {
    CommandRouter router{"novaOps"};
    std::vector<std::string> gpio_calls;
    std::vector<std::string> data_file_calls;
    std::vector<std::string> configs;
    std::vector<std::pair<std::string, bool>> beats;
    // novaMock: its own name is novaMock, but it stands in for novaGround.
    MessageDispatcher dispatcher{NodeTopics::for_node("novaGround"), router,
                                 [this](const std::string& p) { configs.push_back(p); },
                                 [this](const std::string& p, bool retained) { beats.emplace_back(p, retained); }};

    Fixture() {
        router.register_handler("gpio", [this](const json::object& c) { gpio_calls.push_back(json::serialize(c)); },
                                true);
        router.register_handler("data_file",
                                [this](const json::object& c) { data_file_calls.push_back(json::serialize(c)); });
    }
};

const char* kGpio = R"({"source":"novaOps","command":{"type":"gpio","id":17,"state":1}})";
const char* kDataFile = R"({"source":"novaOps","command":{"type":"data_file","action":"stop_data_saving"}})";
}  // namespace

BOOST_FIXTURE_TEST_SUITE(node_topics, Fixture)

BOOST_AUTO_TEST_CASE(the_topics_for_a_node) {
    auto t = NodeTopics::for_node("novaThermo");
    BOOST_TEST(t.command_broadcast == "nova/command");
    BOOST_TEST(t.command_node == "nova/command/novaThermo");
    BOOST_TEST(t.config == "nova/config/novaThermo");
    BOOST_TEST(t.config_status == "nova/config/novaThermo/status");
    BOOST_TEST(t.heartbeat == "nova/heartbeat/backend");
    BOOST_TEST((t.subscriptions() == std::vector<std::string>{"nova/config/novaThermo", "nova/heartbeat/backend",
                                                               "nova/command", "nova/command/novaThermo"}));
}

BOOST_AUTO_TEST_CASE(an_actuator_command_on_the_node_topic_is_carried_out) {
    dispatcher.dispatch("nova/command/novaGround", kGpio, false);
    BOOST_TEST(gpio_calls.size() == 1u);
}

BOOST_AUTO_TEST_CASE(an_actuator_command_on_the_broadcast_topic_is_refused) {
    dispatcher.dispatch("nova/command", kGpio, false);
    BOOST_TEST(gpio_calls.empty());
}

BOOST_AUTO_TEST_CASE(data_file_works_on_the_broadcast_topic) {
    dispatcher.dispatch("nova/command", kDataFile, false);
    BOOST_TEST(data_file_calls.size() == 1u);
}

BOOST_AUTO_TEST_CASE(another_nodes_topics_are_ignored) {
    dispatcher.dispatch("nova/command/novaThermo", kGpio, false);
    dispatcher.dispatch("nova/command/novaMock", kGpio, false);
    dispatcher.dispatch("nova/config/novaThermo", "{}", true);
    dispatcher.dispatch("nova/config/novaGround/status", "{}", true);
    dispatcher.dispatch("nova/telemetry/engine", "{}", false);
    BOOST_TEST(gpio_calls.empty());
    BOOST_TEST(configs.empty());
    BOOST_TEST(beats.empty());
}

BOOST_AUTO_TEST_CASE(config_and_heartbeat_go_to_their_handlers) {
    dispatcher.dispatch("nova/config/novaGround", R"({"config_hash":"x"})", true);
    dispatcher.dispatch("nova/heartbeat/backend", R"({"online":true})", true);
    dispatcher.dispatch("nova/heartbeat/backend", R"({"online":true,"seq":4})", false);
    BOOST_TEST((configs == std::vector<std::string>{R"({"config_hash":"x"})"}));
    BOOST_REQUIRE_EQUAL(beats.size(), 2u);
    BOOST_TEST(beats[0].second);
    BOOST_TEST(!beats[1].second);
}

BOOST_AUTO_TEST_CASE(an_empty_retained_config_is_not_a_config) {
    dispatcher.dispatch("nova/config/novaGround", "", true);
    BOOST_TEST(configs.empty());
}

BOOST_AUTO_TEST_CASE(retained_commands_are_ignored) {
    dispatcher.dispatch("nova/command/novaGround", kGpio, true);
    dispatcher.dispatch("nova/command", kDataFile, true);
    BOOST_TEST(gpio_calls.empty());
    BOOST_TEST(data_file_calls.empty());
}

BOOST_AUTO_TEST_SUITE_END()
