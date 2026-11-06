#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "controllers/gpio_controller.hpp"
#include "core/data_logger.hpp"
#include "fake_digital_io.hpp"
#include "temp_dir.hpp"

namespace json = boost::json;

namespace {
json::object cmd(const char* text) {
    return json::parse(text).as_object();
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string f;
    while (std::getline(in, f, ',')) out.push_back(f);
    return out;
}

struct Fixture {
    FakeDigitalIo io;
    GpioController controller{&io, nullptr};
};
}

BOOST_FIXTURE_TEST_SUITE(gpio_controller, Fixture)

BOOST_AUTO_TEST_CASE(mode_sets_the_direction) {
    controller.handle_command(cmd(R"({"id":17,"mode":"output"})"));
    controller.handle_command(cmd(R"({"id":5,"mode":"input"})"));
    BOOST_TEST((io.calls == std::vector<std::string>{"dir:17:out", "dir:5:in"}));
}

BOOST_AUTO_TEST_CASE(mode_is_case_insensitive) {
    controller.handle_command(cmd(R"({"id":17,"mode":"OUTPUT"})"));
    BOOST_TEST((io.calls == std::vector<std::string>{"dir:17:out"}));
}

BOOST_AUTO_TEST_CASE(an_unknown_mode_changes_nothing) {
    controller.handle_command(cmd(R"({"id":17,"mode":"tristate"})"));
    BOOST_TEST(io.calls.empty());
}

BOOST_AUTO_TEST_CASE(state_writes_one_or_zero) {
    controller.handle_command(cmd(R"({"id":17,"mode":"output"})"));
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    controller.handle_command(cmd(R"({"id":17,"state":false})"));
    controller.handle_command(cmd(R"({"id":17,"state":1})"));
    BOOST_TEST((io.calls == std::vector<std::string>{"dir:17:out", "write:17:1", "write:17:0", "write:17:1"}));
    BOOST_TEST(io.levels[17] == 1);
}

BOOST_AUTO_TEST_CASE(mode_and_state_in_one_command_set_the_direction_first) {
    controller.handle_command(cmd(R"({"id":22,"mode":"output","state":true})"));
    BOOST_TEST((io.calls == std::vector<std::string>{"dir:22:out", "write:22:1"}));
    BOOST_TEST(io.levels[22] == 1);
}

BOOST_AUTO_TEST_CASE(a_command_without_an_id_does_nothing) {
    controller.handle_command(cmd(R"({"mode":"output","state":true})"));
    controller.handle_command(cmd(R"({"id":"17","state":true})"));
    BOOST_TEST(io.calls.empty());
}

BOOST_AUTO_TEST_CASE(a_state_that_is_not_a_bool_or_number_is_ignored) {
    controller.handle_command(cmd(R"({"id":17,"mode":"output","state":"on"})"));
    BOOST_TEST((io.calls == std::vector<std::string>{"dir:17:out"}));
}

BOOST_AUTO_TEST_CASE(no_hardware_means_no_crash) {
    GpioController none(nullptr, nullptr);
    none.handle_command(cmd(R"({"id":17,"mode":"output","state":true})"));
    BOOST_TEST(io.calls.empty());
}

BOOST_AUTO_TEST_CASE(the_actuator_log_records_the_pin_level) {
    TempDir dir;
    std::vector<std::string> headers{"timestamp", "gpio_17"};
    for (int i = 0; i < 16; ++i) headers.push_back("relay_" + std::to_string(i));
    for (int i = 0; i < 16; ++i) headers.push_back("servo_" + std::to_string(i));
    headers.push_back("type_id");
    DataLogger logger(dir.path().string(), {"timestamp"}, headers, {17}, "n");
    BOOST_REQUIRE(logger.start("r"));
    GpioController logged(&io, &logger);

    logged.handle_command(cmd(R"({"id":17,"mode":"output","state":true})"));
    logger.stop();

    std::string text = read_file(logger.last_actuator_path());
    auto last = text.substr(text.find('\n') + 1);
    auto f = fields(last.substr(0, last.find('\n')));
    BOOST_REQUIRE_EQUAL(f.size(), 35u);
    BOOST_TEST(f[1] == "1");        // gpio_17
    BOOST_TEST(f.back() == "gpio");
}

BOOST_AUTO_TEST_SUITE_END()
