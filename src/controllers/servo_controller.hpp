#pragma once

#include <boost/json.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/pwm_driver.hpp"

class ServoController {
public:
    ServoController(PwmDriver* driver, TelemetryStore* telemetry, DataLogger* logger);

    void handle_command(const boost::json::object& cmd);

private:
    struct ServoState {
        uint16_t last_angle = 0;
        bool has_angle = false;
        bool enabled = false;
    };

    bool available() const;

    PwmDriver* driver_ = nullptr;
    TelemetryStore* telemetry_ = nullptr;
    DataLogger* logger_ = nullptr;
    std::unordered_map<int, ServoState> states_;
    std::mutex mutex_;
};
