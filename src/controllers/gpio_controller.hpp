#pragma once

#include <boost/json.hpp>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/data_logger.hpp"
#include "core/pi_io_config.hpp"
#include "interfaces/digital_io.hpp"

class OutputSupervisor;
class StatsRegistry;

// The GPIO pins this node drives and reads, and the `gpio` command.
//
// No pin is claimed until the per-node config arrives (apply()). Until then
// every gpio command is refused. After it, only the configured outputs can be
// commanded, and each output is claimed once, at its safe level (0). A pin
// dropped from the config is driven to 0 and released.
class GpioController {
public:
    // `gpio` may be null (no GPIO chip); `logger`, `stats` may be null.
    GpioController(DigitalIo* gpio, DataLogger* logger, StatsRegistry* stats = nullptr);

    // Not owned. Told about each command carried out, so it can clear a trip.
    void set_supervisor(OutputSupervisor* supervisor);

    // Makes the claimed pins match the config: drops (drive 0 and release)
    // outputs and releases inputs no longer listed, then claims the new ones.
    // Pins that stay as they were are not touched. Returns the pins whose claim
    // failed, as config items ("gpio.outputs.17") with the reason.
    std::vector<ConfigRejection> apply(const std::set<int>& outputs, const std::set<int>& inputs);

    // {"type":"gpio","id":<pin>,"state":<bool or 0/1>}. Refused (logged, and
    // counted in stats) before the first config, for a pin that is not a
    // configured output, or with the old "mode" field.
    void handle_command(const boost::json::object& cmd);

    // Drives a configured output to 0 for the output supervisor and records the
    // trip. Does nothing for a pin that is not a configured output.
    void drive_safe(int pin, const std::string& reason);

    bool configured() const;
    std::vector<int> output_pins() const;  // the claimed outputs, ascending
    std::vector<int> input_pins() const;   // the claimed inputs, ascending

private:
    bool available() const;

    DigitalIo* gpio_ = nullptr;
    DataLogger* logger_ = nullptr;
    StatsRegistry* stats_ = nullptr;
    OutputSupervisor* supervisor_ = nullptr;

    mutable std::mutex mutex_;
    bool configured_ = false;
    std::map<int, int> outputs_;  // claimed output pin -> level last written
    std::set<int> inputs_;        // claimed input pins
};
