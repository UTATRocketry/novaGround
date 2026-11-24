#pragma once

#include <bitset>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/telemetry.hpp"

// The actuator CSV's columns: timestamp, gpio_<pin> for each output pin, relay_0
// to relay_15, servo_0 to servo_15, type_id.
std::vector<std::string> actuator_log_headers(const std::vector<int>& output_pins);

class DataLogger {
public:
    DataLogger(std::string data_dir,
               std::vector<std::string> sensor_headers,
               std::vector<std::string> actuator_headers,
               std::vector<int> gpio_pins,
               std::string file_prefix);

    // Opens a new recording. Its actuator columns are the ones last set with
    // set_actuator_columns (or given to the constructor).
    bool start(const std::string& base_filename);
    // The actuator columns for the next recording: the GPIO outputs configured
    // when it starts. A recording already open keeps its columns.
    void set_actuator_columns(std::vector<std::string> actuator_headers, std::vector<int> gpio_pins);
    void stop();
    bool is_active() const;
    std::string last_sensor_path() const;
    std::string last_actuator_path() const;

    void log_sensor_row(double timestamp_ms, const std::vector<double>& values);

    void update_gpio(int pin, int state);
    void update_relay_state(const std::bitset<16>& state);
    void update_servo(int id, uint16_t angle, bool enabled);
    void log_actuator_snapshot(const std::string& type_id);

private:
    std::string data_dir_;
    std::string file_prefix_;
    std::vector<std::string> sensor_headers_;
    std::vector<std::string> actuator_headers_;
    std::vector<int> gpio_pins_;
    std::vector<std::string> next_actuator_headers_;
    std::vector<int> next_gpio_pins_;

    mutable std::mutex mutex_;
    std::ofstream sensor_file_;
    std::ofstream actuator_file_;
    bool active_ = false;
    std::string last_sensor_path_;
    std::string last_actuator_path_;

    std::map<int, int> gpio_states_;
    std::bitset<16> relay_state_;
    std::map<int, ServoTelemetry> servo_states_;

    std::string sanitize_filename(const std::string& name) const;
    bool open_files(const std::string& base_filename);
    void close_files();

    std::string join_headers(const std::vector<std::string>& headers) const;
};

