#include "linux_gpio.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
// The consumer name the kernel shows for our lines (gpioinfo).
const char* kConsumer = "novaGround";

std::string errno_text() {
    const int e = errno;
    return std::string(std::strerror(e)) + " (errno " + std::to_string(e) + ")";
}
}  // namespace

LinuxGpio::LinuxGpio(const std::string& chipname) {
    chip_ = gpiod_chip_open(chipname.c_str());
    if (!chip_) {
        throw std::runtime_error("Failed to open GPIO chip " + chipname + ": " + errno_text());
    }
    std::cout << "GPIO chip opened: " << chipname << " (no pin is claimed until the config arrives)"
              << std::endl;
}

LinuxGpio::~LinuxGpio() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [pin, info] : pins_) {
        if (info.line) {
            gpiod_line_release(info.line);
        }
    }
    pins_.clear();
    if (chip_) {
        gpiod_chip_close(chip_);
    }
}

void LinuxGpio::release_locked(int pin) {
    auto it = pins_.find(pin);
    if (it == pins_.end()) {
        return;
    }
    if (it->second.line) {
        gpiod_line_release(it->second.line);
    }
    pins_.erase(it);
}

gpiod_line* LinuxGpio::line_for(int pin) {
    if (pin < 0) {
        std::cerr << "GPIO " << pin << ": not a pin number" << std::endl;
        return nullptr;
    }
    gpiod_line* line = gpiod_chip_get_line(chip_, static_cast<unsigned int>(pin));
    if (!line) {
        std::cerr << "GPIO " << pin << ": no such line: " << errno_text() << std::endl;
    }
    return line;
}

bool LinuxGpio::configure_output(int pin, int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int value = level ? 1 : 0;
    auto it = pins_.find(pin);
    if (it != pins_.end() && it->second.direction == Direction::Out) {
        // Already ours as an output: set the level, never release in between
        // (a release and a new request would glitch the line).
        if (gpiod_line_set_value(it->second.line, value) != 0) {
            std::cerr << "GPIO " << pin << ": write of " << value << " failed: " << errno_text() << std::endl;
            return false;
        }
        return true;
    }
    release_locked(pin);

    gpiod_line* line = line_for(pin);
    if (!line) {
        return false;
    }
    // The level is part of the request, so the line never drives anything else.
    if (gpiod_line_request_output(line, kConsumer, value) < 0) {
        std::cerr << "GPIO " << pin << ": claim as output failed: " << errno_text() << std::endl;
        return false;
    }
    pins_[pin] = PinInfo{line, Direction::Out};
    std::cout << "GPIO " << pin << ": claimed as output at " << value << std::endl;
    return true;
}

bool LinuxGpio::configure_input(int pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pins_.find(pin);
    if (it != pins_.end() && it->second.direction == Direction::In) {
        return true;
    }
    release_locked(pin);

    gpiod_line* line = line_for(pin);
    if (!line) {
        return false;
    }
    if (gpiod_line_request_input(line, kConsumer) < 0) {
        std::cerr << "GPIO " << pin << ": claim as input failed: " << errno_text() << std::endl;
        return false;
    }
    pins_[pin] = PinInfo{line, Direction::In};
    std::cout << "GPIO " << pin << ": claimed as input" << std::endl;
    return true;
}

bool LinuxGpio::release(int pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pins_.count(pin)) {
        release_locked(pin);
        std::cout << "GPIO " << pin << ": released" << std::endl;
    }
    return true;
}

bool LinuxGpio::write(int pin, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pins_.find(pin);
    if (it == pins_.end() || it->second.direction != Direction::Out) {
        std::cerr << "GPIO " << pin << ": not claimed as an output" << std::endl;
        return false;
    }
    const int level = value ? 1 : 0;
    if (gpiod_line_set_value(it->second.line, level) != 0) {
        std::cerr << "GPIO " << pin << ": write of " << level << " failed: " << errno_text() << std::endl;
        return false;
    }
    return true;
}

bool LinuxGpio::read(int pin, int& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pins_.find(pin);
    if (it == pins_.end() || it->second.direction != Direction::In) {
        std::cerr << "GPIO " << pin << ": not claimed as an input" << std::endl;
        return false;
    }
    int val = gpiod_line_get_value(it->second.line);
    if (val < 0) {
        std::cerr << "GPIO " << pin << ": read failed: " << errno_text() << std::endl;
        return false;
    }
    value = val;
    return true;
}

std::vector<int> LinuxGpio::get_input_pins() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> result;
    for (const auto& [pin, info] : pins_) {
        if (info.direction == Direction::In) result.push_back(pin);
    }
    return result;
}

std::map<int, int> LinuxGpio::read_all_inputs() {
    std::map<int, int> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [pin, info] : pins_) {
        if (info.direction == Direction::In) {
            int val = gpiod_line_get_value(info.line);
            if (val < 0) {
                std::cerr << "GPIO " << pin << ": read failed: " << errno_text() << std::endl;
                continue;
            }
            result[pin] = val;
        }
    }
    return result;
}

std::map<int, int> LinuxGpio::read_all_outputs() {
    std::map<int, int> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [pin, info] : pins_) {
        if (info.direction == Direction::Out) {
            // For a line requested as an output this returns the level it is
            // being driven to; it does not change it.
            int val = gpiod_line_get_value(info.line);
            if (val < 0) {
                std::cerr << "GPIO " << pin << ": read-back failed: " << errno_text() << std::endl;
                continue;
            }
            result[pin] = val;
        }
    }
    return result;
}
