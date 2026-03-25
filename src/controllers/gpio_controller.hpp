#pragma once

#include <boost/json.hpp>

#include "core/data_logger.hpp"
#include "interfaces/gpio_manager.hpp"

class GpioController {
public:
    GpioController(GPIO_Manager* manager, DataLogger* logger);

    void handle_command(const boost::json::object& cmd);

private:
    bool available() const;

    GPIO_Manager* manager_ = nullptr;
    DataLogger* logger_ = nullptr;
};
