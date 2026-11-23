#pragma once

#include <bitset>
#include <boost/json.hpp>
#include <mutex>

#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/relay_bank.hpp"

class RelayController {
public:
    RelayController(RelayBank* expander, TelemetryStore* telemetry, DataLogger* logger);

    void handle_command(const boost::json::object& cmd);
    void set_initial_state(const std::bitset<16>& state);

private:
    bool available() const;

    RelayBank* expander_ = nullptr;
    TelemetryStore* telemetry_ = nullptr;
    DataLogger* logger_ = nullptr;
    std::bitset<16> relay_state_;
    std::mutex mutex_;
};
