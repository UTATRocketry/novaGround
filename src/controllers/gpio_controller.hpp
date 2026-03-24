#pragma once

#include <boost/json.hpp>

#include "core/telemetry.hpp"
#include "interfaces/gpio_manager.hpp"

class GpioController {
public:
    GpioController(GPIO_Manager* manager, TelemetryStore* telemetry);

    void handle_command(const boost::json::object& cmd);

private:
    bool available() const;

    GPIO_Manager* manager_ = nullptr;
    TelemetryStore* telemetry_ = nullptr;
};
