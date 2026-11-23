# novaGround tests

novaGround is tested at three levels, and none of them needs a Pi or its HATs:

| Level | What | Where |
|---|---|---|
| Unit | The C++ core, drivers and controllers, over fake buses and simulated hardware | `test_*.cpp` → the `nova_core` test |
| Scripts | `nova-pi.sh`, `start.sh` and `run.sh` against stubbed `systemctl`, `tmux` and `sudo` | `bats/`, `scripts_contract.sh` |
| End to end | novaMock (the real program on simulated hardware) against a private broker | `novamock_smoke.sh` |

They run on Linux. On Windows, use WSL or a Debian container.

## Run them

1. Install the build tools. These are the packages CI installs on Debian
   Bookworm:

   ```bash
   sudo apt-get install -y g++ meson ninja-build pkg-config \
     libboost1.81-dev libboost-json1.81-dev libboost-thread1.81-dev libboost-test1.81-dev \
     libpaho-mqtt-dev libpaho-mqttpp-dev libcurl4-openssl-dev \
     mosquitto mosquitto-clients python3 bats
   ```

2. Build without the Pi's hardware libraries:

   ```bash
   meson setup build-test -Dhardware=false
   meson compile -C build-test
   ```

3. Run the unit tests, the scripts contract and the smoke test:

   ```bash
   meson test -C build-test --print-errorlogs
   ```

4. Run the script tests:

   ```bash
   bats tests/bats
   ```

Other ways to run them:

| To | Run |
|---|---|
| Skip the smoke test | `meson test -C build-test --no-suite integration` |
| Run one Boost suite | `./build-test/tests/nova_core_tests --run_test=cli` |
| List every unit test | `./build-test/tests/nova_core_tests --list_content` |
| Run one bats file | `bats tests/bats/start.bats` |
| Build under a sanitizer, as CI does | `meson setup build-asan -Dhardware=false -Db_sanitize=address,undefined -Db_lundef=false` |

`-Dhardware=false` builds only `nova_core`, its tests and novaMock. The Pi
executables need wiringPi, libgpiod and the MCC HAT libraries.

## Unit tests

All `test_*.cpp` files build into one Boost.Test executable,
`nova_core_tests`, registered with meson as `nova_core` (120 s timeout).
`test_main.cpp` only defines the module. The tests link the simulated MCC
library (`src/sim/`) instead of the real one.

| Files | Cover |
|---|---|
| `test_cli.cpp` | Command-line parsing and its usage text |
| `test_command_router.cpp`, `test_json_utils.cpp`, `test_telemetry.cpp` | Incoming commands, JSON handling, telemetry messages |
| `test_actuation_guard.cpp` | Stopped actuation: writes to relays, PWM and digital outputs are dropped, reads still go through |
| `test_actuator_state.cpp` | The actuator-state message, including hardware that's missing or can't be read |
| `test_relay_controller.cpp`, `test_servo_controller.cpp`, `test_gpio_controller.cpp` | The controllers, over fake I/O |
| `test_tca9535.cpp`, `test_pca9685.cpp` | The I2C chip drivers, over a fake bus |
| `test_sampler.cpp`, `test_stoppable_loops.cpp` | The sampling loop and loops that stop cleanly |
| `test_data_logger.cpp`, `test_data_file.cpp` | Recording to CSV |
| `test_stats.cpp` | Run-time stats: their settings, histograms, loop timing and overruns, error counters, and the stats and info messages |
| `test_daqhats_sim.cpp`, `test_scenario.cpp`, `test_waveform.cpp`, `test_sim_actuators.cpp`, `test_sim_hardware.cpp` | The simulator: scenario files, waveforms, the simulated MCC library and the real driver running over it |

Shared test helpers:

| File | Is |
|---|---|
| `fake_i2c_bus.hpp` | An `I2cBus` that acts as a register file. It records every write, answers reads from its registers, and can be told to fail. |
| `fake_digital_io.hpp` | Digital I/O that enforces the real driver's contract: writes need an output pin, reads an input pin. |
| `temp_dir.hpp` | A unique directory under the system temp dir, removed when it goes out of scope. |

## Script tests

> [!NOTE]
> The script tests never start a service or write to `/etc`. Each one runs in
> a scratch `NOVA_PI_ROOT`, with stubs for `systemctl`, `tmux`, `sudo`,
> `journalctl` and `sleep` first on `PATH`. The stubs log their arguments to
> `$CALLS`, so a test can check what a script would have run.

- `bats/nova-pi.bats`, `bats/start.bats`, `bats/run.bats` test each script.
  `bats/helpers.bash` builds the sandbox and the stubs.
- `scripts_contract.sh` checks that every flag the scripts pass to the
  binary (from `nova-args.sh`, and the example `nova-pi.sh` writes to
  `pi.env`) is one `src/utils/cli.cpp` accepts. A mismatch once made a Pi
  restart in a loop. It runs as a meson test, and on its own as
  `bash tests/scripts_contract.sh "$PWD"`.

## Smoke test

`novamock_smoke.sh` (meson suite `integration`, 90 s timeout) starts a
private Mosquitto on a free loopback port, never 1883, runs novaMock with a
scenario, and checks the telemetry it publishes and the CSV it records.
When it fails, it prints novaMock's log with repeated lines collapsed and,
if the log is still long, the middle cut, because meson keeps only the last
100 lines of a failing test's output.
`docs/simulation.md` describes novaMock and its scenario files.

Meson registers it only when `mosquitto` and `python3` are found, and novaMock
only builds when paho is found. Without them, `meson test` doesn't run it, and
says so during `meson setup`.

## CI

`.github/workflows/pr-checks.yml` runs on every PR:

- **scripts** (Ubuntu): `bash -n` and `shellcheck --severity=warning` on
  every `*.sh` and `tests/*.sh`, the scripts contract, the bats suites, and a
  check that `*.sh` and `*.service` use LF line endings.
- **core tests** (Debian Bookworm container, the Pi's OS, on amd64 and native
  arm64): builds with `-Dhardware=false` and runs `meson test` three times
  over: plain, under `address,undefined`, and under `thread`.

> [!NOTE]
> The sanitizer jobs run their tests with ASLR off (`setarch -R`, in a
> container without Docker's default seccomp profile). The runners' kernels
> randomize more address bits than Bookworm's GCC 12 sanitizers allow for.
> Without this, ThreadSanitizer stops with "unexpected memory mapping", and
> AddressSanitizer prints `AddressSanitizer:DEADLYSIGNAL` over and over. The
> "Kernel ASLR entropy" step logs the runner's `vm.mmap_rnd_bits` for
> comparison.
>
> The `thread` jobs also skip the smoke test, because paho and Mosquitto
> aren't built with ThreadSanitizer.

## Writing a test

- **Add a new `.cpp` to the `files(...)` list in `tests/meson.build`.**
  Meson doesn't pick test files up by itself.
- **Use the fakes, not hardware.** Give a driver `FakeI2cBus` or
  `FakeDigitalIo`, or describe the hardware in a simulator scenario.
- **Write files under a `TempDir`**, never into the source tree or a fixed path.
- **New flag in a script?** Add it to `src/utils/cli.cpp` too, or the scripts
  contract fails.
- **Shell scripts:** `#!/usr/bin/env bash`, LF line endings, and clean under
  `shellcheck --severity=warning`. A file that is only sourced starts with
  `# shellcheck shell=bash` instead of a shebang.
