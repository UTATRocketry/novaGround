#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "controllers/gpio_controller.hpp"
#include "core/data_logger.hpp"
#include "core/stats.hpp"
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

StatsIdentity identity() {
    StatsIdentity id;
    id.node = "novaTest";
    id.build_id = "novaTest";
    return id;
}

std::int64_t counter(StatsRegistry& stats, const char* name) {
    return json::parse(stats.take_stats_json()).as_object().at(name).to_number<std::int64_t>();
}

struct Fixture {
    FakeDigitalIo io;
    StatsRegistry stats{identity(), true};
    GpioController controller{&io, nullptr, &stats};
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(gpio_controller, Fixture)

// ── before the config ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(nothing_is_claimed_and_every_command_is_refused_before_the_config) {
    BOOST_TEST(!controller.configured());
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    controller.handle_command(cmd(R"({"id":27,"state":false})"));
    BOOST_TEST(io.calls.empty());
    BOOST_TEST(counter(stats, "refused_commands") == 2);
}

// ── applying the config ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(apply_claims_outputs_at_zero_and_inputs) {
    auto failed = controller.apply({17, 27}, {5});
    BOOST_TEST(failed.empty());
    BOOST_TEST(controller.configured());
    BOOST_TEST((io.calls == std::vector<std::string>{"out:17:0", "out:27:0", "in:5"}));
    BOOST_TEST((controller.output_pins() == std::vector<int>{17, 27}));
    BOOST_TEST((controller.input_pins() == std::vector<int>{5}));
}

BOOST_AUTO_TEST_CASE(applying_the_same_config_again_touches_nothing) {
    controller.apply({17}, {5});
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    io.calls.clear();
    controller.apply({17}, {5});
    BOOST_TEST(io.calls.empty());
    BOOST_TEST(io.levels[17] == 1);  // a pin that stays is never reset
}

BOOST_AUTO_TEST_CASE(a_dropped_output_is_driven_to_zero_then_released) {
    controller.apply({17, 27}, {});
    controller.handle_command(cmd(R"({"id":27,"state":true})"));
    io.calls.clear();
    controller.apply({17}, {});
    BOOST_TEST((io.calls == std::vector<std::string>{"write:27:0", "release:27"}));
    BOOST_TEST(io.levels[27] == 0);
    BOOST_TEST(!io.directions.count(27));
    BOOST_TEST((controller.output_pins() == std::vector<int>{17}));
}

BOOST_AUTO_TEST_CASE(a_dropped_output_is_released_even_if_driving_it_low_fails) {
    controller.apply({27}, {});
    io.failing_writes.insert(27);
    controller.apply({}, {});
    BOOST_TEST(!io.directions.count(27));
    BOOST_TEST(controller.output_pins().empty());
    BOOST_TEST(counter(stats, "gpio_write_failures") == 1);
}

BOOST_AUTO_TEST_CASE(a_dropped_input_is_released) {
    controller.apply({}, {5, 6});
    io.calls.clear();
    controller.apply({}, {6});
    BOOST_TEST((io.calls == std::vector<std::string>{"release:5"}));
}

BOOST_AUTO_TEST_CASE(a_pin_can_move_from_output_to_input) {
    controller.apply({22}, {});
    controller.handle_command(cmd(R"({"id":22,"state":true})"));
    io.calls.clear();
    controller.apply({}, {22});
    BOOST_TEST((io.calls == std::vector<std::string>{"write:22:0", "release:22", "in:22"}));
    BOOST_TEST(io.directions.at(22) == "in");
}

BOOST_AUTO_TEST_CASE(a_failed_claim_is_reported_counted_and_left_unclaimed) {
    io.failing_claims = {17, 5};
    auto failed = controller.apply({17, 27}, {5});
    BOOST_REQUIRE_EQUAL(failed.size(), 2u);
    BOOST_TEST(failed[0].item == "gpio.outputs.17");
    BOOST_TEST(failed[1].item == "gpio.inputs.5");
    BOOST_TEST(!io.directions.count(17));
    BOOST_TEST((controller.output_pins() == std::vector<int>{27}));
    BOOST_TEST(controller.input_pins().empty());
    BOOST_TEST(counter(stats, "gpio_claim_failures") == 2);

    // A command for the pin whose claim failed is refused: it is not an output.
    io.calls.clear();
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    BOOST_TEST(io.calls.empty());

    // The next config tries the claim again.
    io.failing_claims.clear();
    BOOST_TEST(controller.apply({17, 27}, {5}).empty());
    BOOST_TEST((controller.output_pins() == std::vector<int>{17, 27}));
}

BOOST_AUTO_TEST_CASE(an_empty_config_still_counts_as_configured) {
    BOOST_TEST(controller.apply({}, {}).empty());
    BOOST_TEST(controller.configured());
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    BOOST_TEST(io.calls.empty());
}

// ── commands ─────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(state_writes_one_or_zero_to_a_configured_output) {
    controller.apply({17}, {});
    io.calls.clear();
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    controller.handle_command(cmd(R"({"id":17,"state":false})"));
    controller.handle_command(cmd(R"({"id":17,"state":1})"));
    BOOST_TEST((io.calls == std::vector<std::string>{"write:17:1", "write:17:0", "write:17:1"}));
    BOOST_TEST(io.levels[17] == 1);
}

BOOST_AUTO_TEST_CASE(a_command_for_a_pin_that_is_not_a_configured_output_is_refused) {
    controller.apply({17}, {5});
    io.calls.clear();
    controller.handle_command(cmd(R"({"id":27,"state":true})"));  // not in the config
    controller.handle_command(cmd(R"({"id":5,"state":true})"));   // an input
    controller.handle_command(cmd(R"({"id":8,"state":true})"));   // reserved
    controller.handle_command(cmd(R"({"id":-1,"state":true})"));
    controller.handle_command(cmd(R"({"id":4294967313,"state":true})"));  // 2^32 + 17
    BOOST_TEST(io.calls.empty());
    BOOST_TEST(counter(stats, "refused_commands") == 5);
}

BOOST_AUTO_TEST_CASE(the_mode_field_is_refused_and_changes_nothing) {
    controller.apply({17}, {5});
    io.calls.clear();
    controller.handle_command(cmd(R"({"id":17,"mode":"output","state":true})"));
    controller.handle_command(cmd(R"({"id":5,"mode":"output"})"));
    controller.handle_command(cmd(R"({"id":17,"mode":"input"})"));
    BOOST_TEST(io.calls.empty());
    BOOST_TEST(io.directions.at(17) == "out");
    BOOST_TEST(io.directions.at(5) == "in");
}

BOOST_AUTO_TEST_CASE(a_command_without_a_whole_number_id_or_a_state_does_nothing) {
    controller.apply({17}, {});
    io.calls.clear();
    controller.handle_command(cmd(R"({"state":true})"));
    controller.handle_command(cmd(R"({"id":"17","state":true})"));
    controller.handle_command(cmd(R"({"id":17.0,"state":true})"));
    controller.handle_command(cmd(R"({"id":true,"state":true})"));
    controller.handle_command(cmd(R"({"id":17})"));
    controller.handle_command(cmd(R"({"id":17,"state":"on"})"));
    BOOST_TEST(io.calls.empty());
}

BOOST_AUTO_TEST_CASE(no_hardware_means_no_crash_and_claims_fail) {
    GpioController none(nullptr, nullptr);
    auto failed = none.apply({17}, {5});
    BOOST_TEST(failed.size() == 2u);
    none.handle_command(cmd(R"({"id":17,"state":true})"));
    none.drive_safe(17, "max_on_time");
    BOOST_TEST(io.calls.empty());
}

BOOST_AUTO_TEST_CASE(a_failed_write_is_counted_and_the_level_stays) {
    controller.apply({17}, {});
    io.failing_writes.insert(17);
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    BOOST_TEST(io.levels[17] == 0);
    BOOST_TEST(counter(stats, "gpio_write_failures") == 1);
}

BOOST_AUTO_TEST_CASE(drive_safe_writes_zero_to_a_configured_output_only) {
    controller.apply({17}, {5});
    controller.handle_command(cmd(R"({"id":17,"state":true})"));
    io.calls.clear();
    controller.drive_safe(17, "max_on_time");
    controller.drive_safe(5, "max_on_time");   // an input: nothing
    controller.drive_safe(27, "max_on_time");  // not configured: nothing
    BOOST_TEST((io.calls == std::vector<std::string>{"write:17:0"}));
    BOOST_TEST(io.levels[17] == 0);
}

// ── the actuator log ─────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(the_actuator_log_records_the_level_really_written) {
    TempDir dir;
    DataLogger logger(dir.path().string(), {"timestamp"}, actuator_log_headers({17}), {17}, "n");
    BOOST_REQUIRE(logger.start("r"));
    GpioController logged(&io, &logger);
    logged.apply({17}, {});

    logged.handle_command(cmd(R"({"id":17,"state":true})"));
    io.failing_writes.insert(17);
    logged.handle_command(cmd(R"({"id":17,"state":false})"));  // fails: stays 1
    logger.stop();

    std::string text = read_file(logger.last_actuator_path());
    std::vector<std::vector<std::string>> rows;
    std::istringstream lines(text);
    std::string line;
    std::getline(lines, line);  // header
    while (std::getline(lines, line)) rows.push_back(fields(line));
    BOOST_REQUIRE_EQUAL(rows.size(), 2u);
    BOOST_REQUIRE_EQUAL(rows[0].size(), 35u);
    BOOST_TEST(rows[0][1] == "1");  // gpio_17
    BOOST_TEST(rows[0].back() == "gpio");
    BOOST_TEST(rows[1][1] == "1");  // the write of 0 failed: still 1
}

BOOST_AUTO_TEST_SUITE_END()
