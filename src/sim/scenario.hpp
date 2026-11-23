#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sim/waveform.hpp"

struct SimHat {
    int address = 0;
    std::string type;  // "mcc128" or "mcc134"
    std::map<int, Waveform> channels;
};

struct Scenario {
    uint32_t seed = 1;
    std::vector<SimHat> hats;
    std::set<std::pair<int, int>> read_errors;  // (address, channel)
    std::set<int> missing_hats;                 // addresses
    int read_delay_us = 0;
    std::map<int, int> gpio_inputs;  // pin -> level
};

struct ScenarioError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Scenario parse_scenario(const std::string& json_text);
Scenario load_scenario_file(const std::string& path);
Scenario default_scenario();
