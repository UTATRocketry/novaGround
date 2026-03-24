#pragma once

#include <bitset>
#include <boost/json.hpp>
#include <mutex>

#include "core/telemetry.hpp"
#include "interfaces/io_expander.hpp"

class RelayController {
public:
    RelayController(TCA9535* expander, TelemetryStore* telemetry);

    void handle_command(const boost::json::object& cmd);
    void set_initial_state(const std::bitset<16>& state);

private:
    bool available() const;

    TCA9535* expander_ = nullptr;
    TelemetryStore* telemetry_ = nullptr;
    std::bitset<16> relay_state_;
    std::mutex mutex_;
};
