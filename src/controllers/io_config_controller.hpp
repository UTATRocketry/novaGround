#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/pi_io_config.hpp"
#include "interfaces/analog_io.hpp"

class DataLogger;
class GpioController;
class OutputSupervisor;

// Applies the per-node config (nova/config/<node>) and reports the result on
// nova/config/<node>/status (docs/design/pi-io-config.md):
//
//   { "config_hash": "3f9a1c0be27d4a55", "applied_ms": 1795110000000,
//     "rejected": [ { "item": "gpio.outputs.8", "reason": "reserved (SPI0 CE0)" } ],
//     "reserved_pins": [0, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 16, 20, 21, 26],
//     "thermocouples": { "1": { "0": "K", "1": "T" } },
//     "tripped": [ { "output": "gpio:17", "reason": "comms_loss", "time_ms": 1795110123456 } ] }
//
// Before the first config config_hash and applied_ms are null. `thermocouples`
// has every channel of every MCC 134 that opened, with the type it is set to
// (a channel with no configured sensor shows kUnconfiguredTcType). `rejected`
// lists the items of the last config message that were not applied: malformed
// or impossible, or a claim or type write that failed.
class IoConfigController {
public:
    using SystemMs = std::function<std::int64_t()>;
    using Publish = std::function<void(const std::string& status_json)>;

    // `daq` may be null (no HATs); `gpio`, `supervisor` and `logger` may be
    // null too. None is owned. `hats` are the HATs that opened.
    IoConfigController(GpioController* gpio,
                       AnalogIo* daq,
                       std::vector<DaqHatDevice> hats,
                       OutputSupervisor* supervisor,
                       DataLogger* logger,
                       SystemMs system_ms = {});

    // Where the status goes (retained, on nova/config/<node>/status).
    void set_publisher(Publish publish);

    // One message from nova/config/<node>: check it, apply what is usable,
    // publish the status. A message that is not a JSON object changes
    // nothing applied; the status reports it as rejected.
    void handle_message(const std::string& payload);

    std::string status_json();
    // Builds the status and hands it to the publisher (if one is set).
    void publish_status();

private:
    GpioController* gpio_;
    AnalogIo* daq_;
    std::vector<DaqHatDevice> hats_;
    OutputSupervisor* supervisor_;
    DataLogger* logger_;
    SystemMs system_ms_;

    std::mutex mutex_;  // guards everything below; held while a config is applied
    Publish publish_;
    std::optional<std::string> config_hash_;
    std::optional<std::int64_t> applied_ms_;
    std::vector<ConfigRejection> rejected_;
    std::map<int, std::map<int, TcType>> tc_types_;  // MCC 134 address -> channel -> type set
};
