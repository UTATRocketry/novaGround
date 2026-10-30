#pragma once

#include <gpiod.h>
#include <iostream>
#include <stdexcept>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "interfaces/digital_io.hpp"

class LinuxGpio : public DigitalIo {
    // This class is a wrapper for GPIO operations using the gpiod library.
    // It provides methods to set pin direction, read and write pin values,
    // and manage multiple GPIO pins in a thread-safe manner.
public:
    LinuxGpio(const std::string& chipname = "/dev/gpiochip0");
    ~LinuxGpio() override;

    bool set_direction(int pin, const std::string& direction) override;
    bool write(int pin, int value) override;
    bool read(int pin, int& value) override;
    std::vector<int> get_input_pins() override;
    std::map<int, int> read_all_inputs() override;

private:
    struct PinInfo {
        gpiod_line* line = nullptr;
        std::string direction;
    };

    gpiod_chip* chip_;
    std::map<int, PinInfo> pins_;
    std::mutex mutex_;
};