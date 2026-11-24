#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "core/hardware_check.hpp"

namespace hc = hardware_check;
namespace json = boost::json;

namespace {

DaqHatDevice hat(int address, bool thermocouples) {
    DaqHatDevice d;
    d.address = address;
    d.thermocouples = thermocouples;
    return d;
}

const hc::Part& part(const hc::Report& r, const std::string& name) {
    for (const auto& p : r.parts) {
        if (p.name == name) {
            return p;
        }
    }
    throw std::runtime_error("no part " + name);
}

bool any_line_contains(const hc::Report& r, const std::string& text) {
    for (const auto& line : r.lines()) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(hardware_check)

BOOST_AUTO_TEST_CASE(novathermo_with_its_hat_and_gpio_and_no_i2c_chips_is_complete) {
    hc::Found found;
    found.gpio = true;
    found.daq_hats = {hat(1, true)};
    auto r = hc::check("novaThermo", "daq", found);

    BOOST_TEST(r.gpio_ok());
    BOOST_TEST(r.missing().empty());
    BOOST_TEST(!part(r, "relays").expected);
    BOOST_TEST(!part(r, "servo").expected);
    BOOST_TEST(part(r, "daq").detail == "MCC 134 at 1");
    BOOST_TEST(any_line_contains(r, "relay board (TCA9535): not fitted (not expected on novaThermo)"));
    BOOST_TEST(!any_line_contains(r, "ERROR"));
}

BOOST_AUTO_TEST_CASE(novaground_missing_its_relay_board_reports_an_error_and_carries_on) {
    hc::Found found;
    found.gpio = true;
    found.servo = true;
    found.daq_hats = {hat(0, false), hat(1, true)};
    auto r = hc::check("novaGround", "daq,relays,servo", found);

    BOOST_TEST(r.gpio_ok());  // so the program keeps running
    BOOST_TEST(r.missing() == std::vector<std::string>{"relays"});
    BOOST_TEST(any_line_contains(r, "ERROR relay board (TCA9535): NOT FOUND, expected on novaGround. Carrying on"));
    BOOST_TEST(part(r, "daq").detail == "MCC 128 at 0, MCC 134 at 1");
}

BOOST_AUTO_TEST_CASE(gpio_is_always_expected_and_its_absence_is_the_one_that_stops) {
    hc::Found found;  // nothing at all
    auto r = hc::check("novaThermo", "daq", found);
    BOOST_TEST(!r.gpio_ok());
    BOOST_TEST(part(r, "gpio").expected);
    BOOST_TEST(r.missing() == (std::vector<std::string>{"gpio", "daq"}));
    BOOST_TEST(any_line_contains(r, "Stopping: every build needs GPIO"));

    // Even when the build lists nothing.
    auto mock = hc::check("novaMock", "", found);
    BOOST_TEST(part(mock, "gpio").expected);
    BOOST_TEST(!mock.gpio_ok());
}

BOOST_AUTO_TEST_CASE(hardware_found_but_not_expected_is_used_and_said_so) {
    hc::Found found;
    found.gpio = true;
    found.relays = true;
    auto r = hc::check("novaThermo", "", found);
    BOOST_TEST(r.missing().empty());
    BOOST_TEST(any_line_contains(r, "relay board (TCA9535): found, not expected on novaThermo; using it"));
}

BOOST_AUTO_TEST_CASE(the_expected_list_tolerates_spaces_and_reports_unknown_names) {
    hc::Found found;
    found.gpio = true;
    auto r = hc::check("novaGround", " daq , relayz ,,", found);
    BOOST_TEST(part(r, "daq").expected);
    BOOST_TEST(!part(r, "relays").expected);
    BOOST_TEST(r.unknown_expected == std::vector<std::string>{"relayz"});
    BOOST_TEST(any_line_contains(r, "ignored unknown name 'relayz'"));
}

BOOST_AUTO_TEST_CASE(the_json_lists_every_part_and_what_is_missing) {
    hc::Found found;
    found.gpio = true;
    found.daq_hats = {hat(0, false)};
    auto j = hc::check("novaGround", "daq,relays,servo", found).to_json();

    BOOST_TEST(j.at("build_id").as_string() == "novaGround");
    const auto& parts = j.at("parts").as_object();
    BOOST_TEST(parts.size() == 4u);
    BOOST_TEST(parts.at("gpio").at("found").as_bool());
    BOOST_TEST(parts.at("daq").at("detail").as_string() == "MCC 128 at 0");
    BOOST_TEST(!parts.at("relays").at("found").as_bool());
    BOOST_TEST(parts.at("relays").at("expected").as_bool());
    const auto& missing = j.at("missing").as_array();
    BOOST_REQUIRE_EQUAL(missing.size(), 2u);
    BOOST_TEST(missing.at(0).as_string() == "relays");
    BOOST_TEST(missing.at(1).as_string() == "servo");
}

BOOST_AUTO_TEST_SUITE_END()
