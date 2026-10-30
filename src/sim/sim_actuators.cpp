#include "sim_actuators.hpp"

#include <iostream>

bool SimRelayBank::write_output(const std::bitset<16>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    ++writes_;
    return true;
}

std::bitset<16> SimRelayBank::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

int SimRelayBank::writes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writes_;
}

void SimPwmDriver::writeMicroseconds(uint8_t channel, uint16_t microseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    pulse_us_[channel] = microseconds;
}

bool SimPwmDriver::disablePWM(uint8_t channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    pulse_us_[channel] = 0;
    return true;
}

uint16_t SimPwmDriver::microseconds(uint8_t channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pulse_us_.find(channel);
    return it == pulse_us_.end() ? 0 : it->second;
}

SimDigitalIo::SimDigitalIo(std::map<int, int> input_levels)
    : input_levels_(std::move(input_levels)) {}

bool SimDigitalIo::set_direction(int pin, const std::string& direction) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (direction != "in" && direction != "out") {
        std::cerr << "Invalid direction for GPIO " << pin << ": " << direction << "\n";
        return false;
    }
    directions_[pin] = direction;
    return true;
}

bool SimDigitalIo::write(int pin, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = directions_.find(pin);
    if (it == directions_.end() || it->second != "out") {
        std::cerr << "Pin " << pin << " not configured as output\n";
        return false;
    }
    output_levels_[pin] = value;
    return true;
}

bool SimDigitalIo::read(int pin, int& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = directions_.find(pin);
    if (it == directions_.end() || it->second != "in") {
        std::cerr << "Pin " << pin << " not configured as input\n";
        return false;
    }
    auto level = input_levels_.find(pin);
    value = level == input_levels_.end() ? 0 : level->second;
    return true;
}

std::vector<int> SimDigitalIo::get_input_pins() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> pins;
    for (const auto& [pin, direction] : directions_) {
        if (direction == "in") pins.push_back(pin);
    }
    return pins;
}

std::map<int, int> SimDigitalIo::read_all_inputs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<int, int> result;
    for (const auto& [pin, direction] : directions_) {
        if (direction != "in") continue;
        auto level = input_levels_.find(pin);
        result[pin] = level == input_levels_.end() ? 0 : level->second;
    }
    return result;
}

int SimDigitalIo::output_level(int pin) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = output_levels_.find(pin);
    return it == output_levels_.end() ? 0 : it->second;
}
