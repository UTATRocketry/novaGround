#include "telemetry.hpp"

#include <algorithm>
#include <shared_mutex>
#include <mutex>

void TelemetryStore::set_sensors(std::vector<SensorSample> samples) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    sensors_ = std::move(samples);
}

std::vector<SensorSample> TelemetryStore::snapshot_sensors() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return sensors_;
}

void TelemetryStore::set_gpio_states(std::map<int, int> states) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    gpio_states_ = std::move(states);
}

std::map<int, int> TelemetryStore::snapshot_gpio_states() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return gpio_states_;
}

void TelemetryStore::set_relay_state(std::bitset<16> state) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    relay_state_ = state;
}

std::bitset<16> TelemetryStore::snapshot_relay_state() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return relay_state_;
}

void TelemetryStore::upsert_servo_state(int id, uint16_t angle, bool enabled) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(servo_states_.begin(), servo_states_.end(),
                           [id](const ServoTelemetry& s) { return s.id == id; });
    if (it == servo_states_.end()) {
        servo_states_.push_back(ServoTelemetry{id, angle, enabled});
    } else {
        it->angle = angle;
        it->enabled = enabled;
    }
}

std::vector<ServoTelemetry> TelemetryStore::snapshot_servo_states() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return servo_states_;
}

