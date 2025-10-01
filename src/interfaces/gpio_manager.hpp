#pragma once

#include <gpiod.h>
#include <iostream>
#include <stdexcept>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class GPIO_Manager {
    // This class is a wrapper for GPIO operations using the gpiod library.
    // It provides methods to set pin direction, read and write pin values,
    // and manage multiple GPIO pins in a thread-safe manner.
public:
    GPIO_Manager(const std::string& chipname = "/dev/gpiochip0");
    ~GPIO_Manager();

    bool set_direction(int pin, const std::string& direction);
    bool write(int pin, int value);
    bool read(int pin, int& value);
    std::vector<int> get_input_pins();
    std::map<int, int> read_all_inputs();

private:
    struct PinInfo {
        gpiod_line* line = nullptr;
        std::string direction;
    };

    gpiod_chip* chip_;
    std::map<int, PinInfo> pins_;
    std::mutex mutex_;
};