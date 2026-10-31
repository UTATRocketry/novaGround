# Simulated hardware (novaMock)

`novaMock` is the real novaGround program, built against simulated hardware
instead of the Pi's. The sampler, logger, publisher, command router and the
relay, servo, GPIO and data-file controllers are the same code that runs on the
Pi; only the bottom layer is replaced:

| Layer | On the Pi | In novaMock |
|---|---|---|
| MCC 128 / 134 | MCC's `libdaqhats` | `libdaqhats_sim` (`src/sim/daqhats_sim.cpp`), the same `MccDaqHats` driver on top |
| Relays (TCA9535) | `LinuxI2cBus` | `SimRelayBank` (in memory) |
| Servos (PCA9685) | `LinuxI2cBus`, SMBus | `SimPwmDriver` (in memory) |
| GPIO | libgpiod | `SimDigitalIo` (in memory) |

`main()` asks `build_hardware()` (declared in `src/interfaces/hardware.hpp`) for
a `Hardware`: the DAQ, the opened HATs, the servo, the relays and the GPIO. Which
machine that is gets decided when building: `novaGround` and `novaThermo` link
`src/drivers/hardware_real.cpp`, `novaMock` links `src/sim/hardware_sim.cpp`.

It starts the same way (`./build/novaMock --broker localhost`) and needs only
paho and Boost to build, so it builds on a laptop or in CI.

## Scenario files

The simulated sensors and faults come from a JSON file named by the
`NOVA_SIM_SCENARIO` environment variable. Without it, a default scenario is
used: two MCC 128s with the same sine waves as the backend's `novaSim.py` GCS
sensors. If the variable names a file that cannot be read, the simulator fails
(and says why) rather than quietly simulating something else.

```json
{
  "seed": 1,
  "hats": [
    { "address": 0, "type": "mcc128",
      "channels": { "0": { "wave": "sine", "min": 0.99, "max": 3.99, "period": 20, "noise": 0.02 },
                    "3": { "wave": "flat", "value": 2.5 } } },
    { "address": 1, "type": "mcc134", "channels": { "0": { "wave": "flat", "value": 21.5 } } }
  ],
  "faults": {
    "read_errors": [ { "address": 0, "channel": 5 } ],
    "missing_hats": [ 2 ],
    "read_delay_us": 0
  },
  "gpio_inputs": { "5": 1, "6": 0 }
}
```

- **`hats`**: the HATs on the bus. `type` is `mcc128` (8 channels) or `mcc134`
  (4 channels). A channel with no entry reads as a flat 0.
- **Waveforms** use the backend simulator's model: `flat`, `sine`, `square`,
  `triangle`, `sawtooth`, `s_curve`, `ramp` and `noise`, with `min`, `max`,
  `period` (seconds), `noise` (added +/- amplitude) and, for `flat`, `value`.
  `seed` makes the noise repeatable. Time is measured from the start of the
  program.
- **`faults`**: `read_errors` make one channel fail every read (published as
  NaN, like a real failed read); `missing_hats` lists hats that are in the file
  but not found on the bus; `read_delay_us` slows every read, to provoke
  sample-period overruns.
- **`gpio_inputs`**: the level each input pin reads (default low).

## Tests

- `meson test -C build` (with `-Dhardware=false` if you do not have the Pi's
  libraries) runs the unit tests, including the simulated library and the real
  `MccDaqHats` driver over it.
- `novamock_smoke` (suite `integration`) starts a private mosquitto on a free
  loopback port, runs novaMock with a scenario, and checks the published
  telemetry and a recorded CSV. It needs `mosquitto`, `mosquitto_sub`,
  `mosquitto_pub` and `python3`, and never touches port 1883.

The simulated headers in `src/sim/include/daqhats/` are not MCC's; they declare
only what novaGround calls, with this simulator's own constants. A real build
uses MCC's headers.
