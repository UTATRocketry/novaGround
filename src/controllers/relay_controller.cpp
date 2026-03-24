#include "relay_controller.hpp"

#include <iostream>

#include "utils/json_utils.hpp"

RelayController::RelayController(TCA9535* expander, TelemetryStore* telemetry)
    : expander_(expander), telemetry_(telemetry) {}

bool RelayController::available() const {
    return expander_ && expander_->is_ready();
}

void RelayController::handle_command(const boost::json::object& cmd) {
    if (!available()) {
        std::cerr << "Relay controller unavailable." << std::endl;
        return;
    }

    auto pin_opt = get_int(cmd, "id");
    auto state_opt = get_bool(cmd, "state");
    if (!pin_opt || !state_opt) {
        std::cerr << "Relay command missing id or state." << std::endl;
        return;
    }

    int pin = static_cast<int>(*pin_opt);
    if (pin < 0 || pin >= 16) {
        std::cerr << "Invalid relay pin: " << pin << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        relay_state_.set(pin, *state_opt);
    }

    if (!expander_->write_output(relay_state_)) {
        std::cerr << "Failed to write relay state." << std::endl;
        return;
    }

    if (telemetry_) {
        telemetry_->set_relay_state(relay_state_);
    }
}

void RelayController::set_initial_state(const std::bitset<16>& state) {
    relay_state_ = state;
    if (telemetry_) {
        telemetry_->set_relay_state(relay_state_);
    }
}
