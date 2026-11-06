#pragma once

#include <map>
#include <string>
#include <vector>

// What GpioController and the GPIO sampler need from the GPIO hardware.
// Directions are the strings "in" and "out", as before.
class DigitalIo {
public:
    virtual ~DigitalIo() = default;

    virtual bool set_direction(int pin, const std::string& direction) = 0;
    virtual bool write(int pin, int value) = 0;
    virtual bool read(int pin, int& value) = 0;
    virtual std::vector<int> get_input_pins() = 0;
    virtual std::map<int, int> read_all_inputs() = 0;
};
