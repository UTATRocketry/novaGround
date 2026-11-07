#pragma once

#include <chrono>
#include <string>

#include "core/stats.hpp"
#include "interfaces/digital_io.hpp"
#include "interfaces/pwm_driver.hpp"
#include "interfaces/relay_bank.hpp"

// Where the actuators can be read back from. Any of them may be null: that
// hardware is not available, and its section is left out of the message.
struct ActuatorSources {
    RelayBank* relays = nullptr;
    PwmDriver* servo = nullptr;
    DigitalIo* gpio = nullptr;
};

// The PCA9685 has 16 channels; ServoController accepts ids 0 to 15.
constexpr int kServoChannels = 16;

// Reads every actuator back from its driver and returns the nova/state/<node>
// message as JSON text. This is what the hardware holds, not what was commanded:
// it reads the chips' registers (and the GPIO lines), it writes nothing.
//
//   { "schema": 1, "time_ms": ...,
//     "relays": { "ok": true, "mask": 65535 },
//     "servos": { "ok": true, "channels": [ { "id": 0, "output": "off" },
//                                           { "id": 1, "output": "pulse", "pulse_us": 1498 } ] },
//     "gpios":  { "ok": true, "outputs": [ { "pin_id": 17, "state": 0 } ] } }
//
// `ok` is false when a read failed; each failed read is also counted in
// `stats` (actuator_read_errors) when it is given. `time_ms` is the system
// clock when the reads started.
std::string actuator_state_json(const ActuatorSources& sources,
                                StatsRegistry* stats,
                                std::chrono::system_clock::time_point now =
                                    std::chrono::system_clock::now());
