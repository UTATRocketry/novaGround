#include "sim_actuators.hpp"

#include <iostream>

bool SimRelayBank::write_output(const std::bitset<16>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    ++writes_;
    return true;
}

bool SimRelayBank::read_output(std::bitset<16>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state = state_;
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

bool SimPwmDriver::read_channel(uint8_t channel, PwmChannelReadback& readback) {
    if (channel > 15) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pulse_us_.find(channel);
    readback = PwmChannelReadback{};
    if (it != pulse_us_.end() && it->second != 0) {
        readback.output = PwmOutput::Pulse;
        readback.microseconds = it->second;
    }
    return true;
}

uint16_t SimPwmDriver::microseconds(uint8_t channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pulse_us_.find(channel);
    return it == pulse_us_.end() ? 0 : it->second;
}

SimDigitalIo::SimDigitalIo(std::map<int, int> input_levels)
    : input_levels_(std::move(input_levels)) {}

bool SimDigitalIo::configure_output(int pin, int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = claimed_.find(pin);
    if (it != claimed_.end() && it->second == Direction::Out) {
        // Already an output: like the real driver, just set the level.
        if (failing_writes_[pin]) {
            std::cerr << "Simulated GPIO: write to " << pin << " failed (injected)" << std::endl;
            return false;
        }
        output_levels_[pin] = level;
        return true;
    }
    claimed_.erase(pin);  // an input is released first
    if (failing_claims_[pin]) {
        std::cerr << "Simulated GPIO: claim of " << pin << " failed (injected)" << std::endl;
        return false;
    }
    claimed_[pin] = Direction::Out;
    output_levels_[pin] = level;
    return true;
}

bool SimDigitalIo::configure_input(int pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = claimed_.find(pin);
    if (it != claimed_.end() && it->second == Direction::In) {
        return true;
    }
    claimed_.erase(pin);
    if (failing_claims_[pin]) {
        std::cerr << "Simulated GPIO: claim of " << pin << " failed (injected)" << std::endl;
        return false;
    }
    claimed_[pin] = Direction::In;
    return true;
}

bool SimDigitalIo::release(int pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    // The line keeps the level it was last driven to, as on the Pi; it is no
    // longer this program's to drive.
    claimed_.erase(pin);
    return true;
}

bool SimDigitalIo::write(int pin, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = claimed_.find(pin);
    if (it == claimed_.end() || it->second != Direction::Out) {
        std::cerr << "Pin " << pin << " not configured as output" << std::endl;
        return false;
    }
    if (failing_writes_[pin]) {
        std::cerr << "Simulated GPIO: write to " << pin << " failed (injected)" << std::endl;
        return false;
    }
    output_levels_[pin] = value;
    return true;
}

bool SimDigitalIo::read(int pin, int& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = claimed_.find(pin);
    if (it == claimed_.end() || it->second != Direction::In) {
        std::cerr << "Pin " << pin << " not configured as input" << std::endl;
        return false;
    }
    auto level = input_levels_.find(pin);
    value = level == input_levels_.end() ? 0 : level->second;
    return true;
}

std::vector<int> SimDigitalIo::get_input_pins() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> pins;
    for (const auto& [pin, direction] : claimed_) {
        if (direction == Direction::In) pins.push_back(pin);
    }
    return pins;
}

std::map<int, int> SimDigitalIo::read_all_inputs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<int, int> result;
    for (const auto& [pin, direction] : claimed_) {
        if (direction != Direction::In) continue;
        auto level = input_levels_.find(pin);
        result[pin] = level == input_levels_.end() ? 0 : level->second;
    }
    return result;
}

std::map<int, int> SimDigitalIo::read_all_outputs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<int, int> result;
    for (const auto& [pin, direction] : claimed_) {
        if (direction != Direction::Out) continue;
        auto level = output_levels_.find(pin);
        result[pin] = level == output_levels_.end() ? 0 : level->second;
    }
    return result;
}

int SimDigitalIo::output_level(int pin) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = output_levels_.find(pin);
    return it == output_levels_.end() ? 0 : it->second;
}

bool SimDigitalIo::claimed(int pin) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return claimed_.count(pin) > 0;
}

void SimDigitalIo::fail_claims_of(int pin, bool fail) {
    std::lock_guard<std::mutex> lock(mutex_);
    failing_claims_[pin] = fail;
}

void SimDigitalIo::fail_writes_to(int pin, bool fail) {
    std::lock_guard<std::mutex> lock(mutex_);
    failing_writes_[pin] = fail;
}
