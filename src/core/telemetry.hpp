#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <vector>

struct SensorSample {
    int hat_id = 0;
    int channel_id = 0;
    double value = 0.0;
    double timestamp_ms = 0.0;
};

struct ServoTelemetry {
    int id = 0;
    uint16_t angle = 0;
    bool enabled = false;
};

class TelemetryStore {
public:
    void set_sensors(std::vector<SensorSample> samples);
    std::vector<SensorSample> snapshot_sensors() const;

    void set_gpio_states(std::map<int, int> states);
    std::map<int, int> snapshot_gpio_states() const;

    void set_relay_state(std::bitset<16> state);
    std::bitset<16> snapshot_relay_state() const;

    void upsert_servo_state(int id, uint16_t angle, bool enabled);
    std::vector<ServoTelemetry> snapshot_servo_states() const;

private:
    mutable std::shared_mutex mutex_;
    std::vector<SensorSample> sensors_;
    std::map<int, int> gpio_states_;
    std::bitset<16> relay_state_;
    std::vector<ServoTelemetry> servo_states_;
};
