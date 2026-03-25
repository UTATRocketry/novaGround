#include "gpio_controller.hpp"

#include <iostream>

#include "utils/json_utils.hpp"

GpioController::GpioController(GPIO_Manager* manager, DataLogger* logger)
    : manager_(manager), logger_(logger) {}

bool GpioController::available() const {
    return manager_ != nullptr;
}

void GpioController::handle_command(const boost::json::object& cmd) {
    if (!available()) {
        std::cerr << "GPIO controller unavailable." << std::endl;
        return;
    }

    auto pin_opt = get_int(cmd, "id");
    if (!pin_opt) {
        std::cerr << "GPIO command missing id." << std::endl;
        return;
    }

    int pin = static_cast<int>(*pin_opt);
    if (auto mode_opt = get_string(cmd, "mode")) {
        std::string mode = to_lower(*mode_opt);
        if (mode == "input") {
            manager_->set_direction(pin, "in");
        } else if (mode == "output") {
            manager_->set_direction(pin, "out");
        } else {
            std::cerr << "Invalid GPIO mode: " << mode << std::endl;
        }
    }

    if (auto state_opt = get_bool(cmd, "state")) {
        manager_->write(pin, *state_opt ? 1 : 0);
        if (logger_) {
            logger_->update_gpio(pin, *state_opt ? 1 : 0);
        }
    }

    if (logger_) {
        logger_->log_actuator_snapshot("gpio");
    }
}
