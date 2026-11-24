#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "interfaces/digital_io.hpp"

// Models the contract of the real driver (drivers/linux_gpio.cpp): a claim as an
// output sets the level at once, a failed claim leaves the pin unclaimed, write
// needs an output pin and read an input pin. Claims and writes can be made to
// fail.
class FakeDigitalIo : public DigitalIo {
public:
    // "out:<pin>:<level>", "in:<pin>", "release:<pin>" and "write:<pin>:<value>",
    // for every call, failed or not.
    std::vector<std::string> calls;
    std::map<int, std::string> directions;  // claimed pins: "in" or "out"
    std::map<int, int> levels;              // output levels, and input levels to read
    std::set<int> failing_claims;
    std::set<int> failing_writes;
    bool throw_on_read_all = false;

    bool configure_output(int pin, int level) override {
        calls.push_back("out:" + std::to_string(pin) + ":" + std::to_string(level));
        auto it = directions.find(pin);
        if (it != directions.end() && it->second == "out") {
            if (failing_writes.count(pin)) return false;
            levels[pin] = level;
            return true;
        }
        directions.erase(pin);
        if (failing_claims.count(pin)) return false;
        directions[pin] = "out";
        levels[pin] = level;
        return true;
    }
    bool configure_input(int pin) override {
        calls.push_back("in:" + std::to_string(pin));
        auto it = directions.find(pin);
        if (it != directions.end() && it->second == "in") return true;
        directions.erase(pin);
        if (failing_claims.count(pin)) return false;
        directions[pin] = "in";
        return true;
    }
    bool release(int pin) override {
        calls.push_back("release:" + std::to_string(pin));
        directions.erase(pin);
        return true;
    }
    bool write(int pin, int value) override {
        calls.push_back("write:" + std::to_string(pin) + ":" + std::to_string(value));
        auto it = directions.find(pin);
        if (it == directions.end() || it->second != "out") return false;
        if (failing_writes.count(pin)) return false;
        levels[pin] = value;
        return true;
    }
    bool read(int pin, int& value) override {
        auto it = directions.find(pin);
        if (it == directions.end() || it->second != "in") return false;
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
    std::map<int, int> read_all_outputs() override {
        if (throw_on_read_all) throw std::runtime_error("fake gpio failure");
        std::map<int, int> out;
        for (const auto& [pin, dir] : directions) {
            if (dir == "out") out[pin] = levels[pin];
        }
        return out;
    }
};
