#include "interfaces/hardware.hpp"

#include <iostream>

#include "drivers/mcc_daqhats.hpp"
#include "sim/daqhats_sim.hpp"
#include "sim/sim_actuators.hpp"

// The simulated machine. The DAQ is the real MccDaqHats driver, which novaMock
// links against libdaqhats_sim instead of MCC's library; the outputs are kept in
// memory.
Hardware build_hardware(const std::vector<unsigned int>& output_pins,
                        const std::vector<unsigned int>& input_pins) {
    Hardware hw;
    std::cout << "Simulated hardware: HATs from the scenario, outputs kept in memory." << std::endl;

    hw.daq = std::make_unique<MccDaqHats>();
    hw.daq_hats = discover_and_open(*hw.daq);

    hw.servo = std::make_unique<SimPwmDriver>();

    // The same startup state as the real chips: all relays on, output pins low.
    auto relays = std::make_unique<SimRelayBank>();
    for (int i = 0; i < 16; i++) {
        hw.relay_state.set(i, true);
    }
    relays->write_output(hw.relay_state);
    hw.relays = std::move(relays);

    auto gpio = std::make_unique<SimDigitalIo>(daqhats_sim::gpio_inputs());
    for (auto pin : output_pins) {
        gpio->set_direction(pin, "out");
        gpio->write(pin, 0);
    }
    for (auto pin : input_pins) {
        gpio->set_direction(pin, "in");
    }
    hw.gpio = std::move(gpio);
    return hw;
}
