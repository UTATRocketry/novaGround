#pragma once

#include <bitset>
#include <memory>
#include <vector>

#include "core/data_logger.hpp"
#include "interfaces/digital_io.hpp"
#include "interfaces/i2c_bus.hpp"
#include "interfaces/pwm_driver.hpp"
#include "interfaces/relay_bank.hpp"

// The outputs main() wires the controllers to. Any of servo/relays/gpio is
// null if that hardware is not available. Members are destroyed in reverse
// order, so the buses (declared first) outlive the drivers that use them.
struct Actuators {
    std::vector<std::unique_ptr<I2cBus>> buses;
    std::unique_ptr<PwmDriver> servo;
    std::unique_ptr<RelayBank> relays;
    std::bitset<16> relay_state;  // what was written at startup
    std::unique_ptr<DigitalIo> gpio;
};

// Brings up the outputs and puts them in their startup state: all relays on,
// the output pins low. Records that state in `logger`.
//
// There are two implementations; a target links exactly one:
//   actuators_real.cpp  the Pi's I2C chips and gpiochip (novaGround, novaThermo)
//   actuators_sim.cpp   in-memory outputs (novaMock)
Actuators build_actuators(DataLogger& logger,
                          const std::vector<unsigned int>& output_pins,
                          const std::vector<unsigned int>& input_pins);
