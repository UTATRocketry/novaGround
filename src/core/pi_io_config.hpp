#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "interfaces/analog_io.hpp"

// The per-node I/O config: the retained message the backend publishes on
// nova/config/<node> (docs/design/pi-io-config.md in Nova-Collected). It says
// which GPIO pins are outputs and inputs, the limits on GPIO and relay outputs,
// each MCC 134 channel's thermocouple type, and the heartbeat timeout:
//
//   { "config_hash": "3f9a1c0be27d4a55",
//     "gpio": { "outputs": { "17": { "max_on_ms": 1800000, "on_comms_loss": "safe" } },
//               "inputs": [5] },
//     "relays": { "3": { "max_on_ms": null, "on_comms_loss": "hold" } },
//     "daq": { "1": { "0": "K", "1": "T" } },
//     "heartbeat": { "timeout_ms": 2000 } }
//
// Every value here is a setting from the backend config. The Pi only rejects
// what is malformed or what the hardware can't take (AGENTS.md rule 11): an
// unknown key, a pin outside BCM 0-27, a reserved pin, a pin listed as both
// input and output, a thermocouple type the library doesn't know, or a daq
// address that isn't an MCC 134. Each rejected item is left out and reported;
// everything else is applied.

// ── Reserved pins ────────────────────────────────────────────────────────────
// Facts about the HATs and the board, not settings: the Pi never claims these
// pins, on any build (a HAT can move between Pis, so every build reserves the
// whole list). See "Reserved pins" in the design doc for the sources.
struct ReservedPin {
    int pin;
    const char* use;  // what uses it, for the rejection reason
};
const std::vector<ReservedPin>& reserved_pins();
// What uses `pin`, or null if it is not reserved.
const char* reserved_pin_use(int pin);
// The highest BCM pin number on the 40-pin header.
constexpr int kMaxBcmPin = 27;
// Relay channels on the TCA9535 board.
constexpr int kRelayChannels = 16;

// Thermocouple types (TcType, kUnconfiguredTcType) are in interfaces/analog_io.hpp.

// ── The config ───────────────────────────────────────────────────────────────
struct OutputLimits {
    std::optional<std::int64_t> max_on_ms;  // none: no limit
    bool safe_on_comms_loss = false;        // "safe" (true) or "hold" (false, the default)

    bool operator==(const OutputLimits&) const = default;
};

struct PiIoConfig {
    std::optional<std::string> config_hash;
    std::map<int, OutputLimits> gpio_outputs;  // BCM pin -> limits
    std::set<int> gpio_inputs;                 // BCM pins
    std::map<int, OutputLimits> relays;        // relay channel -> limits; others behave as before
    std::map<int, std::map<int, TcType>> daq;  // MCC 134 address -> channel -> type
    std::optional<std::int64_t> heartbeat_timeout_ms;  // none: no heartbeat section
};

struct ConfigRejection {
    std::string item;    // e.g. "gpio.outputs.8"
    std::string reason;  // e.g. "reserved (SPI0 CE0)"

    bool operator==(const ConfigRejection&) const = default;
};

struct ParsedIoConfig {
    // False when the message as a whole can't be used (not JSON, or not a JSON
    // object). Nothing in `config` is then meant to be applied.
    bool usable = false;
    PiIoConfig config;
    std::vector<ConfigRejection> rejected;
};

// Parses and checks one nova/config/<node> payload. `mcc134_addresses` are the
// addresses of the MCC 134 HATs this Pi opened; a daq entry for any other
// address is rejected. Never throws.
ParsedIoConfig parse_pi_io_config(const std::string& payload, const std::set<int>& mcc134_addresses);
