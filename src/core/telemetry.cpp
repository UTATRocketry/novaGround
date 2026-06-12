#include "telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <shared_mutex>

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

// ---- FAS ------------------------------------------------------------------

static double now_ms() {
    using namespace std::chrono;
    return static_cast<double>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void TelemetryStore::push_fas_adc(const FasAdcSample& sample) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Keep a rolling window of 1024 samples across all boards.
    if (fas_adc_.size() >= 1024) {
        fas_adc_.erase(fas_adc_.begin());
    }
    fas_adc_.push_back(sample);

    // Mirror ch0 into the shared SensorSample store so the existing publisher
    // and DataLogger see it. hat_id = 100 + board_id; channel_id = 0 for ch0,
    // 1 for ch1.
    double ts = now_ms();
    for (int ch = 0; ch < 2; ++ch) {
        SensorSample ss;
        ss.hat_id       = 100 + sample.board_id;
        ss.channel_id   = ch;
        ss.value        = sample.v[ch];
        ss.timestamp_ms = ts;

        // Upsert by (hat_id, channel_id).
        bool found = false;
        for (auto& s : sensors_) {
            if (s.hat_id == ss.hat_id && s.channel_id == ss.channel_id) {
                s = ss;
                found = true;
                break;
            }
        }
        if (!found) sensors_.push_back(ss);
    }
}

std::vector<FasAdcSample> TelemetryStore::snapshot_fas_adc(size_t max_samples) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (fas_adc_.size() <= max_samples) return fas_adc_;
    return std::vector<FasAdcSample>(
        fas_adc_.end() - static_cast<std::ptrdiff_t>(max_samples),
        fas_adc_.end());
}

void TelemetryStore::upsert_fas_board(FasBoardStatus status) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    fas_boards_[status.key] = std::move(status);
}

std::vector<FasBoardStatus> TelemetryStore::snapshot_fas_boards() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<FasBoardStatus> out;
    out.reserve(fas_boards_.size());
    for (const auto& [k, v] : fas_boards_) out.push_back(v);
    return out;
}

void TelemetryStore::set_fas_imc(FasImcStatus status) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    fas_imc_ = status;
}

FasImcStatus TelemetryStore::snapshot_fas_imc() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return fas_imc_;
}

