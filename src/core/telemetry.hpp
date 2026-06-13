#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

struct SensorSample {
    int hat_id = 0; // technically this is the hat address
    int channel_id = 0;
    double value = 0.0;
    double timestamp_ms = 0.0;
};

struct ServoTelemetry {
    int id = 0;
    uint16_t angle = 0;
    bool enabled = false;
};

// ---- FAS-specific telemetry types -----------------------------------------

// One timestamped 2-channel ADC sample from an EPB.
struct FasAdcSample {
    int board_id   = 0;      // EPB board_id (0-7)
    uint32_t t_us  = 0;      // firmware timestamp on FMC timeline (µs, wraps ~71 min)
    double v[2]    = {};     // volts, ch0 and ch1
    double mA[2]   = {};     // milliamps, ch0 and ch1
};

// Online / offline status for one board on the CAN bus.
struct FasBoardStatus {
    std::string key;         // "EPB:0", "FMC:0", "PMB:0", etc.
    bool online      = false;
    uint32_t uptime_ms = 0;
};

// IMC (igniter) arm/disarm state echoed by the EPB.
struct FasImcStatus {
    int board_id       = 0;
    bool armed         = false;
    bool arm_line      = false;
    bool disarm_line   = false;
};

class TelemetryStore {
public:
    // ---- Existing interface (unchanged) ------------------------------------
    void set_sensors(std::vector<SensorSample> samples);
    std::vector<SensorSample> snapshot_sensors() const;

    void set_gpio_states(std::map<int, int> states);
    std::map<int, int> snapshot_gpio_states() const;

    void set_relay_state(std::bitset<16> state);
    std::bitset<16> snapshot_relay_state() const;

    void upsert_servo_state(int id, uint16_t angle, bool enabled);
    std::vector<ServoTelemetry> snapshot_servo_states() const;

    // ---- FAS interface ----------------------------------------------------

    // Append incoming ADC samples. Also mirrors each sample into sensors_ as a
    // SensorSample with hat_id = 100 + board_id so the existing publisher loop
    // and DataLogger pick them up without modification.
    void push_fas_adc(const FasAdcSample& sample);
    // Returns up to max_samples of the most recently pushed samples, newest last.
    std::vector<FasAdcSample> snapshot_fas_adc(size_t max_samples = 256) const;

    void upsert_fas_board(FasBoardStatus status);
    std::vector<FasBoardStatus> snapshot_fas_boards() const;

    void set_fas_imc(FasImcStatus status);
    FasImcStatus snapshot_fas_imc() const;

private:
    mutable std::shared_mutex mutex_;
    std::vector<SensorSample> sensors_;
    std::map<int, int> gpio_states_;
    std::bitset<16> relay_state_;
    std::vector<ServoTelemetry> servo_states_;

    // FAS state
    std::vector<FasAdcSample> fas_adc_;
    std::map<std::string, FasBoardStatus> fas_boards_;
    FasImcStatus fas_imc_;
};
