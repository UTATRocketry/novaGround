#pragma once

#include <boost/json.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/telemetry.hpp"
#include "interfaces/servo.hpp"

class ServoController {
public:
    ServoController(Adafruit_PWMServoDriver* driver, TelemetryStore* telemetry);

    void handle_command(const boost::json::object& cmd);

private:
    struct ServoState {
        uint16_t last_angle = 0;
        bool has_angle = false;
        bool enabled = false;
    };

    bool available() const;

    Adafruit_PWMServoDriver* driver_ = nullptr;
    TelemetryStore* telemetry_ = nullptr;
    std::unordered_map<int, ServoState> states_;
    std::mutex mutex_;
};
