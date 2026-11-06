#pragma once

#include <bitset>
#include <memory>
#include <vector>

#include "interfaces/analog_io.hpp"
#include "interfaces/digital_io.hpp"
#include "interfaces/i2c_bus.hpp"
#include "interfaces/pwm_driver.hpp"
#include "interfaces/relay_bank.hpp"

// Everything main() reads from or drives, behind the interfaces. Any of servo,
// relays and gpio is null if that hardware is not available, and daq_hats is
// empty if no HAT opened. Members are destroyed in reverse order, so the buses
// (declared first) outlive the drivers that use them.
struct Hardware {
    std::vector<std::unique_ptr<I2cBus>> buses;

    std::unique_ptr<AnalogIo> daq;
    std::vector<DaqHatDevice> daq_hats;  // the HATs that opened

    std::unique_ptr<PwmDriver> servo;

    std::unique_ptr<RelayBank> relays;
    std::bitset<16> relay_state;  // what was written at startup

    std::unique_ptr<DigitalIo> gpio;
};

// Brings the hardware up and puts the outputs in their startup state: all
// relays on, the output pins low. A device that is missing is logged and left
// null; it never stops the program.
//
// Which hardware this is gets decided when building. A target links exactly one
// implementation:
//   drivers/hardware_real.cpp  the Pi: MCC HATs, I2C chips, gpiochip
//                              (novaGround, novaThermo)
//   sim/hardware_sim.cpp       simulated: libdaqhats_sim and in-memory outputs
//                              (novaMock)
Hardware build_hardware(const std::vector<unsigned int>& output_pins,
                        const std::vector<unsigned int>& input_pins);
