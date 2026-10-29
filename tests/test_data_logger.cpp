#include <boost/test/unit_test.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/data_logger.hpp"
#include "temp_dir.hpp"

namespace {
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) out.push_back(l);
    return out;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string f;
    while (std::getline(in, f, ',')) out.push_back(f);
    return out;
}

std::vector<std::string> actuator_headers() {
    std::vector<std::string> h{"timestamp", "gpio_17", "gpio_27"};
    for (int i = 0; i < 16; ++i) h.push_back("relay_" + std::to_string(i));
    for (int i = 0; i < 16; ++i) h.push_back("servo_" + std::to_string(i));
    h.push_back("type_id");
    return h;
}

struct Fixture {
    TempDir dir;
    DataLogger logger{dir.path().string(),
                      {"timestamp", "hat0_ch0", "hat0_ch1"},
                      actuator_headers(),
                      {17, 27},
                      "node"};
};
}

BOOST_FIXTURE_TEST_SUITE(data_logger, Fixture)

BOOST_AUTO_TEST_CASE(start_creates_prefixed_files_with_headers) {
    BOOST_REQUIRE(logger.start("run1"));
    BOOST_TEST(logger.is_active());
    BOOST_TEST(logger.last_sensor_path() == dir.path().string() + "/node_run1_sensors.csv");
    BOOST_TEST(logger.last_actuator_path() == dir.path().string() + "/node_run1_actuators.csv");
    logger.stop();
    BOOST_TEST(!logger.is_active());
    BOOST_TEST(lines(read_file(logger.last_sensor_path())).at(0) == "timestamp,hat0_ch0,hat0_ch1");
    BOOST_TEST(split(lines(read_file(logger.last_actuator_path())).at(0)).size() == 36u);
}

BOOST_AUTO_TEST_CASE(filenames_are_sanitised) {
    BOOST_REQUIRE(logger.start("../a b/c"));
    BOOST_TEST(logger.last_sensor_path() == dir.path().string() + "/node_.._a_b_c_sensors.csv");
    BOOST_REQUIRE(logger.start(""));
    BOOST_TEST(logger.last_sensor_path() == dir.path().string() + "/node_data_sensors.csv");
}

BOOST_AUTO_TEST_CASE(sensor_rows_only_while_active) {
    logger.log_sensor_row(1, {1.0, 2.0});  // not started: dropped
    BOOST_REQUIRE(logger.start("r"));
    logger.log_sensor_row(100, {1.5, 2.0});
    logger.stop();
    logger.log_sensor_row(200, {3.0, 4.0});  // stopped: dropped
    auto l = lines(read_file(logger.last_sensor_path()));
    BOOST_REQUIRE_EQUAL(l.size(), 2u);
    BOOST_TEST(l[1] == "100,1.5,2");
}

BOOST_AUTO_TEST_CASE(sensor_row_size_mismatch_and_nan) {
    BOOST_REQUIRE(logger.start("r"));
    logger.log_sensor_row(1, {1.0});  // wrong size: dropped
    logger.log_sensor_row(2, {std::nan(""), 5.0});
    logger.stop();
    auto l = lines(read_file(logger.last_sensor_path()));
    BOOST_REQUIRE_EQUAL(l.size(), 2u);
    BOOST_TEST(l[1] == "2,NaN,5");
}

BOOST_AUTO_TEST_CASE(actuator_snapshot_reflects_state) {
    logger.update_gpio(17, 1);
    std::bitset<16> relays;
    relays.set(2);
    logger.update_relay_state(relays);
    logger.update_servo(4, 77, true);
    logger.update_servo(5, 99, false);  // a disabled servo logs 0
    BOOST_REQUIRE(logger.start("r"));
    logger.log_actuator_snapshot("cmd");
    logger.stop();

    auto l = lines(read_file(logger.last_actuator_path()));
    BOOST_REQUIRE_EQUAL(l.size(), 2u);
    auto f = split(l[1]);
    BOOST_REQUIRE_EQUAL(f.size(), 36u);
    BOOST_TEST(f[1] == "1");            // gpio_17
    BOOST_TEST(f[2] == "0");            // gpio_27 never set
    BOOST_TEST(f[3 + 2] == "1");        // relay_2
    BOOST_TEST(f[3 + 3] == "0");        // relay_3
    BOOST_TEST(f[3 + 16 + 4] == "77");  // servo_4
    BOOST_TEST(f[3 + 16 + 5] == "0");   // servo_5 disabled
    BOOST_TEST(f[35] == "cmd");
}

BOOST_AUTO_TEST_CASE(restart_truncates_the_previous_file) {
    BOOST_REQUIRE(logger.start("r"));
    logger.log_sensor_row(1, {1.0, 2.0});
    BOOST_REQUIRE(logger.start("r"));
    logger.stop();
    BOOST_TEST(lines(read_file(logger.last_sensor_path())).size() == 1u);
}

BOOST_AUTO_TEST_CASE(start_fails_when_directory_is_a_file) {
    auto blocker = dir.path() / "blocker";
    { std::ofstream(blocker) << "x"; }
    DataLogger bad(blocker.string(), {"timestamp"}, {"timestamp"}, {}, "n");
    BOOST_TEST(!bad.start("r"));
    BOOST_TEST(!bad.is_active());
}

BOOST_AUTO_TEST_SUITE_END()
