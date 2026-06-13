#include "data_logger.hpp"

#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

using namespace std::chrono;

DataLogger::DataLogger(std::string data_dir,
                       std::vector<std::string> sensor_headers,
                       std::vector<std::string> actuator_headers,
                       std::vector<int> gpio_pins,
                       std::string file_prefix)
    : data_dir_(std::move(data_dir)),
      sensor_headers_(std::move(sensor_headers)),
      actuator_headers_(std::move(actuator_headers)),
      gpio_pins_(std::move(gpio_pins)),
      file_prefix_(std::move(file_prefix)) {}

bool DataLogger::start(const std::string& base_filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    close_files();
    active_ = open_files(base_filename);
    return active_;
}

void DataLogger::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    close_files();
    active_ = false;
}

bool DataLogger::is_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

std::string DataLogger::last_sensor_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_sensor_path_;
}

std::string DataLogger::last_actuator_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_actuator_path_;
}

std::string DataLogger::last_fas_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_fas_path_;
}

void DataLogger::log_sensor_row(double timestamp_ms, const std::vector<double>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !sensor_file_.is_open()) {
        return;
    }

    size_t expected = sensor_headers_.size() > 0 ? sensor_headers_.size() - 1 : 0;
    if (values.size() != expected) {
        std::cerr << "Sensor row size mismatch. Expected " << expected
                  << ", got " << values.size() << std::endl;
        return;
    }

    sensor_file_ << static_cast<long long>(timestamp_ms);
    for (double value : values) {
        if (std::isnan(value)) {
            sensor_file_ << "," << "NaN";
        } else {
            sensor_file_ << "," << value;
        }
    }
    sensor_file_ << "\n";
}

void DataLogger::log_fas_row(int board_id, uint32_t t_us,
                              double ch0_V, double ch1_V,
                              double ch0_mA, double ch1_mA) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !fas_file_.is_open()) {
        return;
    }
    fas_file_ << board_id << "," << t_us << ","
              << ch0_V  << "," << ch1_V  << ","
              << ch0_mA << "," << ch1_mA << "\n";
}

void DataLogger::update_gpio(int pin, int state) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpio_states_[pin] = state;
}

void DataLogger::update_relay_state(const std::bitset<16>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    relay_state_ = state;
}

void DataLogger::update_servo(int id, uint16_t angle, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    servo_states_[id] = ServoTelemetry{id, angle, enabled};
}

void DataLogger::log_actuator_snapshot(const std::string& type_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !actuator_file_.is_open()) {
        return;
    }

    auto now = system_clock::now();
    double timestamp_ms = duration_cast<milliseconds>(now.time_since_epoch()).count();

    actuator_file_ << static_cast<long long>(timestamp_ms);

    for (int pin : gpio_pins_) {
        auto it = gpio_states_.find(pin);
        int value = (it != gpio_states_.end()) ? it->second : 0;
        actuator_file_ << "," << value;
    }

    for (int i = 0; i < 16; ++i) {
        actuator_file_ << "," << (relay_state_.test(i) ? 1 : 0);
    }

    for (int i = 0; i < 16; ++i) {
        auto it = servo_states_.find(i);
        if (it == servo_states_.end() || !it->second.enabled) {
            actuator_file_ << "," << 0;
        } else {
            actuator_file_ << "," << it->second.angle;
        }
    }

    actuator_file_ << "," << type_id << "\n";
}

std::string DataLogger::sanitize_filename(const std::string& name) const {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            sanitized.push_back(c);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "data";
    }
    return sanitized;
}

bool DataLogger::open_files(const std::string& base_filename) {
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    if (ec) {
        std::cerr << "Failed to create data directory: " << data_dir_ << std::endl;
        return false;
    }

    std::string safe_name = sanitize_filename(base_filename);
    std::string safe_prefix = sanitize_filename(file_prefix_);
    if (!safe_prefix.empty()) {
        safe_name = safe_prefix + "_" + safe_name;
    }
    std::string sensor_path = data_dir_ + "/" + safe_name + "_sensors.csv";
    std::string actuator_path = data_dir_ + "/" + safe_name + "_actuators.csv";
    std::string fas_path = data_dir_ + "/" + safe_name + "_FAS.csv";

    sensor_file_.open(sensor_path, std::ios::out | std::ios::trunc);
    actuator_file_.open(actuator_path, std::ios::out | std::ios::trunc);
    fas_file_.open(fas_path, std::ios::out | std::ios::trunc);

    if (!sensor_file_.is_open() || !actuator_file_.is_open() || !fas_file_.is_open()) {
        std::cerr << "Failed to open data files for writing." << std::endl;
        close_files();
        return false;
    }

    sensor_file_ << join_headers(sensor_headers_) << "\n";
    actuator_file_ << join_headers(actuator_headers_) << "\n";
    fas_file_ << "board_id,t_us,ch0_V,ch1_V,ch0_mA,ch1_mA\n";

    last_sensor_path_ = sensor_path;
    last_actuator_path_ = actuator_path;
    last_fas_path_ = fas_path;

    return true;
}

void DataLogger::close_files() {
    if (sensor_file_.is_open()) {
        sensor_file_.close();
    }
    if (actuator_file_.is_open()) {
        actuator_file_.close();
    }
    if (fas_file_.is_open()) {
        fas_file_.close();
    }
}

std::string DataLogger::join_headers(const std::vector<std::string>& headers) const {
    std::ostringstream oss;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << headers[i];
    }
    return oss.str();
}

