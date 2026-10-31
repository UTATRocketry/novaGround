#include "interfaces/hardware.hpp"

#include <iostream>

#include "drivers/io_expander.hpp"
#include "drivers/linux_gpio.hpp"
#include "drivers/linux_i2c_bus.hpp"
#include "drivers/mcc_daqhats.hpp"
#include "drivers/servo.hpp"

namespace {
const int kI2CAddr = 0x20;
}

Hardware build_hardware(const std::vector<unsigned int>& output_pins,
                        const std::vector<unsigned int>& input_pins) {
    Hardware hw;

    hw.daq = std::make_unique<MccDaqHats>();
    hw.daq_hats = discover_and_open(*hw.daq);

    // One bus (one file descriptor) per chip, as before the drivers took a bus.
    hw.buses.push_back(std::make_unique<LinuxI2cBus>("/dev/i2c-1", LinuxI2cBus::Transfer::Smbus));
    auto servo_driver = std::make_unique<Adafruit_PWMServoDriver>(*hw.buses.back());
    if (servo_driver->begin()) {
        servo_driver->setPWMFreq(50);
        hw.servo = std::move(servo_driver);
    }

    hw.buses.push_back(std::make_unique<LinuxI2cBus>("/dev/i2c-1"));
    auto io_expander = std::make_unique<TCA9535>(*hw.buses.back(), kI2CAddr);
    if (io_expander->is_ready()) {
        bool ok = io_expander->configure_port(0, 0x00) &&
                  io_expander->configure_port(1, 0x00);
        for (int i = 0; i < 16; i++) {
            hw.relay_state.set(i, true);
        }
        ok = ok && io_expander->write_output(hw.relay_state);
        if (ok) {
            hw.relays = std::move(io_expander);
        } else {
            std::cerr << "IO expander setup failed; disabling relay control." << std::endl;
        }
    } else {
        std::cerr << "IO expander not available." << std::endl;
    }

    try {
        auto gpio = std::make_unique<LinuxGpio>("/dev/gpiochip0");
        for (auto pin : output_pins) {
            gpio->set_direction(pin, "out");
            gpio->write(pin, 0);
        }
        for (auto pin : input_pins) {
            gpio->set_direction(pin, "in");
        }
        hw.gpio = std::move(gpio);
    } catch (const std::exception& e) {
        std::cerr << "GPIO Manager initialization failed: " << e.what() << std::endl;
    }

    return hw;
}
