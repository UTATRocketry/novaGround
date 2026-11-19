#pragma once

#include <functional>
#include <map>

#include "sim/scenario.hpp"

// Control surface of libdaqhats_sim, the simulated stand-in for MCC's libdaqhats
// (hat_list, mcc128_*, mcc134_*; see include/daqhats/). Without any setup the
// first API call loads the scenario named by the NOVA_SIM_SCENARIO environment
// variable, or default_scenario() if it is unset. A scenario file that cannot
// be read makes hat_list fail (negative result) and says why on stderr, so a
// typo in the path is loud rather than silently simulating something else.
namespace daqhats_sim {

// Replace the simulated hardware. Nothing is opened afterwards and the
// waveform clock restarts at zero.
void load(const Scenario& scenario);

// Forget everything; the next API call loads from the environment again.
void reset();

// The scenario's fixed GPIO input levels (pin -> level), so the simulated GPIO
// can share the scenario file. Empty if no scenario can be loaded.
std::map<int, int> gpio_inputs();

// Seconds since the simulation started, for tests. An empty function goes back
// to the real (steady) clock.
void set_clock(std::function<double()> seconds);

}  // namespace daqhats_sim
