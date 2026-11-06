#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "interfaces/digital_io.hpp"

// Models the contract of the real driver: write needs an output pin, read an
// input pin.
class FakeDigitalIo : public DigitalIo {
public:
    std::vector<std::string> calls;  // "dir:<pin>:<in|out>" and "write:<pin>:<value>"
    std::map<int, std::string> directions;
    std::map<int, int> levels;
    bool throw_on_read_all = false;

    bool set_direction(int pin, const std::string& direction) override {
        calls.push_back("dir:" + std::to_string(pin) + ":" + direction);
        directions[pin] = direction;
        return true;
    }
    bool write(int pin, int value) override {
        calls.push_back("write:" + std::to_string(pin) + ":" + std::to_string(value));
        if (directions[pin] != "out") return false;
        levels[pin] = value;
        return true;
    }
    bool read(int pin, int& value) override {
        if (directions[pin] != "in") return false;
        value = levels[pin];
        return true;
    }
    std::vector<int> get_input_pins() override {
        std::vector<int> pins;
        for (const auto& [pin, dir] : directions) {
            if (dir == "in") pins.push_back(pin);
        }
        return pins;
    }
    std::map<int, int> read_all_inputs() override {
        if (throw_on_read_all) throw std::runtime_error("fake gpio failure");
        std::map<int, int> out;
        for (int pin : get_input_pins()) out[pin] = levels[pin];
        return out;
    }
};
