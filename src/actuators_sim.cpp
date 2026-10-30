#include "actuators.hpp"

#include <iostream>

#include "sim/daqhats_sim.hpp"
#include "sim/sim_actuators.hpp"

Actuators build_actuators(DataLogger& logger,
                          const std::vector<unsigned int>& output_pins,
                          const std::vector<unsigned int>& input_pins) {
    Actuators a;
    std::cout << "Simulated outputs: relays, servos and GPIO are kept in memory." << std::endl;

    a.servo = std::make_unique<SimPwmDriver>();

    // The same startup state as the real chips: all relays on, output pins low.
    auto relays = std::make_unique<SimRelayBank>();
    for (int i = 0; i < 16; i++) {
        a.relay_state.set(i, true);
    }
    relays->write_output(a.relay_state);
    a.relays = std::move(relays);
    logger.update_relay_state(a.relay_state);

    auto gpio = std::make_unique<SimDigitalIo>(daqhats_sim::gpio_inputs());
    for (auto pin : output_pins) {
        gpio->set_direction(pin, "out");
        gpio->write(pin, 0);
        logger.update_gpio(static_cast<int>(pin), 0);
    }
    for (auto pin : input_pins) {
        gpio->set_direction(pin, "in");
    }
    a.gpio = std::move(gpio);
    return a;
}
