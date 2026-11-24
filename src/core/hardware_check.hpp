#pragma once

#include <string>
#include <vector>

#include <boost/json.hpp>

#include "interfaces/analog_io.hpp"

// What a build expects to find on its Pi, against what build_hardware() found.
//
// Each build is made for certain hardware, which is part of what the build is
// (like NOVA_BUILD_ID), set next to it in meson.build:
//   novaGround  GPIO, the DAQ HATs, the TCA9535 relay board and the PCA9685 servo driver
//   novaThermo  GPIO and the DAQ HATs
//   novaMock    GPIO, relays and servos, all simulated (its DAQ HATs come from
//               the scenario, so they aren't expected)
//
// Only GPIO is required: every Pi has a gpiochip, and without one the program
// stops. Anything else that is missing is reported as an error and the program
// carries on without it: its commands are refused, its readings are absent.
// Hardware that is found but not expected is used, and reported.
namespace hardware_check {

// The parts, by the names used in NOVA_EXPECTED_HARDWARE and in the report.
inline constexpr const char* kDaq = "daq";
inline constexpr const char* kRelays = "relays";
inline constexpr const char* kServo = "servo";
inline constexpr const char* kGpio = "gpio";

struct Found {
    std::vector<DaqHatDevice> daq_hats;
    bool relays = false;
    bool servo = false;
    bool gpio = false;
};

struct Part {
    std::string name;    // kDaq, kRelays, kServo or kGpio
    std::string label;   // for people: "DAQ HATs", "relay board (TCA9535)"
    bool expected = false;
    bool found = false;
    std::string detail;  // e.g. "MCC 128 at 0, MCC 134 at 1"
};

struct Report {
    std::string build_id;
    std::vector<Part> parts;
    // Names in the expected list that aren't parts: a build-file mistake,
    // reported and otherwise ignored.
    std::vector<std::string> unknown_expected;

    bool gpio_ok() const;
    // Expected parts that weren't found.
    std::vector<std::string> missing() const;
    // One line per part, for the log. Missing expected parts start with "ERROR".
    std::vector<std::string> lines() const;
    boost::json::object to_json() const;
};

// `expected` is the comma-separated NOVA_EXPECTED_HARDWARE, e.g. "daq,relays".
// GPIO is always expected, whether or not it is listed.
Report check(const std::string& build_id, const std::string& expected, const Found& found);

}  // namespace hardware_check
