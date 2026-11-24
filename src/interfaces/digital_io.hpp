#pragma once

#include <map>
#include <vector>

// What GpioController and the GPIO sampler need from the GPIO hardware.
//
// A pin is either unclaimed, claimed as an output, or claimed as an input. The
// program claims only the pins the per-node config lists (see
// controllers/gpio_controller.hpp); an unclaimed pin is not driven at all.
class DigitalIo {
public:
    virtual ~DigitalIo() = default;

    // Claims `pin` as an output driving `level` from the moment it is claimed.
    // A pin that is already an output is set to `level` without being released
    // first, so it never glitches. A pin that is an input is released and
    // claimed again as an output. False if the claim failed; the pin is then
    // left unclaimed (never half-claimed).
    virtual bool configure_output(int pin, int level) = 0;

    // Claims `pin` as an input. False if the claim failed; the pin is then left
    // unclaimed.
    virtual bool configure_input(int pin) = 0;

    // Gives the pin back. Writes nothing: drive an output to its safe level
    // first. Releasing a pin that is not claimed does nothing and succeeds.
    virtual bool release(int pin) = 0;

    // Sets a claimed output. False if the pin is not an output or the write failed.
    virtual bool write(int pin, int value) = 0;
    // Reads a claimed input. False if the pin is not an input or the read failed.
    virtual bool read(int pin, int& value) = 0;
    virtual std::vector<int> get_input_pins() = 0;
    virtual std::map<int, int> read_all_inputs() = 0;

    // The level each output pin is being driven to, read back from the line.
    // Changes nothing. A pin that cannot be read is left out.
    virtual std::map<int, int> read_all_outputs() = 0;
};
