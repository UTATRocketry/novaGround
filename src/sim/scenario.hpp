#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sim/waveform.hpp"

// What the simulated hardware looks like and what it produces. Read from a
// JSON file (the NOVA_SIM_SCENARIO environment variable, see daqhats_sim.hpp),
// so the same file can describe the sensors for the Python simulator too:
//
//   {
//     "seed": 1,
//     "hats": [
//       { "address": 0, "type": "mcc128",
//         "channels": { "0": { "wave": "sine", "min": 0.99, "max": 3.99,
//                              "period": 20, "noise": 0.02 } } },
//       { "address": 1, "type": "mcc134", "channels": { "0": { "wave": "flat", "value": 21.5 } } }
//     ],
//     "faults": {
//       "read_errors": [ { "address": 0, "channel": 3 } ],
//       "missing_hats": [ 1 ],
//       "read_delay_us": 0
//     },
//     "gpio_inputs": { "5": 1, "6": 0 }
//   }
//
// A channel that is not listed reads as a flat 0. A "missing" hat is in the
// file but not found on the bus.
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

// Throws ScenarioError on malformed JSON, an unknown hat type, or values of
// the wrong type.
Scenario parse_scenario(const std::string& json_text);
Scenario load_scenario_file(const std::string& path);

// One MCC 128 at address 0 with the same sine channels as the Python
// simulator's default GCS sensors.
Scenario default_scenario();
