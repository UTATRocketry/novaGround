#pragma once

#include <boost/json.hpp>

#include "core/data_logger.hpp"
#include "drivers/linux_gpio.hpp"

class GpioController {
public:
    GpioController(LinuxGpio* manager, DataLogger* logger);

    void handle_command(const boost::json::object& cmd);

private:
    bool available() const;

    LinuxGpio* manager_ = nullptr;
    DataLogger* logger_ = nullptr;
};
