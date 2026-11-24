#include "relay_controller.hpp"

#include <iostream>

#include "core/output_supervisor.hpp"
#include "utils/json_utils.hpp"

RelayController::RelayController(RelayBank* expander, TelemetryStore* telemetry, DataLogger* logger)
    : expander_(expander), telemetry_(telemetry), logger_(logger) {}

void RelayController::set_supervisor(OutputSupervisor* supervisor) {
    std::lock_guard<std::mutex> lock(mutex_);
    supervisor_ = supervisor;
}

bool RelayController::available() const {
    return expander_ && expander_->is_ready();
}

bool RelayController::write_locked(const std::bitset<16>& next, const char* type_id) {
    if (!expander_->write_output(next)) {
        return false;
    }
    relay_state_ = next;
    if (telemetry_) {
        telemetry_->set_relay_state(relay_state_);
    }
    if (logger_) {
        logger_->update_relay_state(relay_state_);
        logger_->log_actuator_snapshot(type_id);
    }
    return true;
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

    if (*pin_opt < 0 || *pin_opt >= 16) {
        std::cerr << "Invalid relay pin: " << *pin_opt << std::endl;
        return;
    }
    const int pin = static_cast<int>(*pin_opt);

    std::lock_guard<std::mutex> lock(mutex_);
    std::bitset<16> next = relay_state_;
    next.set(pin, *state_opt);
    if (!write_locked(next, "relay")) {
        std::cerr << "Failed to write relay state; relay " << pin << " stays as it was." << std::endl;
        return;
    }
    if (supervisor_) {
        supervisor_->command_applied(relay_output(pin));
    }
}

void RelayController::set_initial_state(const std::bitset<16>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    relay_state_ = state;
    if (telemetry_) {
        telemetry_->set_relay_state(relay_state_);
    }
    if (logger_) {
        logger_->update_relay_state(relay_state_);
    }
}

void RelayController::drive_safe(int channel, const std::string& reason) {
    if (!available() || channel < 0 || channel >= 16) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::bitset<16> next = relay_state_;
    next.set(channel, kRelaySafeBit);
    if (!write_locked(next, "trip")) {
        std::cerr << "Relay " << channel << ": de-energizing it for a trip (" << reason << ") failed" << std::endl;
        return;
    }
    if (supervisor_) {
        supervisor_->record_trip(relay_output(channel), reason);
    }
}
