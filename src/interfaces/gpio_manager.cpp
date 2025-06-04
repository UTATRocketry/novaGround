#include "gpio_manager.hpp"
#include <iostream>

GPIO_Manager::GPIO_Manager(const std::string& chipname) {
    chip_ = gpiod_chip_open(chipname.c_str());
    if (!chip_) {
        throw std::runtime_error("Failed to open GPIO chip: " + chipname);
    }
    else {
        std::cout << "GPIO chip opened successfully: " << chipname << std::endl;
    }
}

GPIO_Manager::~GPIO_Manager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [pin, info] : pins_) {
        if (info.line) {
            gpiod_line_release(info.line);
        }
    }
    if (chip_) {
        gpiod_chip_close(chip_);
    }
}

bool GPIO_Manager::set_direction(int pin, const std::string& direction) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (pins_.count(pin) && pins_[pin].line) {
        gpiod_line_release(pins_[pin].line);
    }

    gpiod_line* line = gpiod_chip_get_line(chip_, pin);
    if (!line) {
        std::cerr << "Failed to get line for GPIO " << pin << "\n";
        return false;
    }

    int result;
    if (direction == "out") {
        result = gpiod_line_request_output(line, "GpioControl", 0);
        std::cout << "Set GPIO " << pin << " as output\n";
    } else if (direction == "in") {
        result = gpiod_line_request_input(line, "GpioControl");
        std::cout << "Set GPIO " << pin << " as input\n";
    } else {
        std::cerr << "Invalid direction for GPIO " << pin << ": " << direction << "\n";
        return false;
    }

    if (result < 0) {
        std::cerr << "Failed to set direction for GPIO " << pin << "\n";
        return false;
    }

    pins_[pin] = {line, direction};
    return true;
}

bool GPIO_Manager::write(int pin, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pins_.count(pin) || pins_[pin].direction != "out") {
        std::cerr << "Pin " << pin << " not configured as output\n";
        return false;
    }
    bool success = gpiod_line_set_value(pins_[pin].line, value) == 0;
    if (!success) {
        std::cerr << "Failed to write to GPIO " << pin << "\n";
        return false;
    }
    std::cout << "Wrote " << value << " to GPIO " << pin << "\n";
    return success;
}

bool GPIO_Manager::read(int pin, int& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pins_.count(pin) || pins_[pin].direction != "in") {
        std::cerr << "Pin " << pin << " not configured as input\n";
        return false;
    }
    int val = gpiod_line_get_value(pins_[pin].line);
    if (val < 0) {
        std::cerr << "Failed to read GPIO " << pin << "\n";
        return false;
    }
    value = val;
    std::cout << "Read GPIO " << pin << ": " << value << "\n";
    return true;
}

std::vector<int> GPIO_Manager::get_input_pins() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> result;
    for (const auto& [pin, info] : pins_) {
        if (info.direction == "in") result.push_back(pin);
    }
    return result;
}
std::map<int, int> GPIO_Manager::read_all_inputs() {
    std::map<int, int> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [pin, info] : pins_) {
        if (info.direction == "in") {
            int val = gpiod_line_get_value(info.line);
            if (val < 0) {
                std::cerr << "Failed to read GPIO " << pin << "\n";
                continue;
            }
            result[pin] = val;
        }
    }
    return result;
}
