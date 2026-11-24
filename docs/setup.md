# Setting up and running novaGround

Building, running and configuring novaGround on a Raspberry Pi.

**What this document answers**

- How to install the toolchain and libraries, automatically or by hand
- Which executables the build makes, and how they differ
- Every command-line option, and where the service gets its settings
- How commands, GPIO pins and thermocouple types reach a Pi, and which pins are reserved
- What the watchdogs do and how they are turned on
- How to address stacked DAQ HATs

> [!IMPORTANT]
> Use a Raspberry Pi 4 with **Raspberry Pi OS (Legacy, 64-bit)**. The MCC
> daqhats library doesn't work on Debian Trixie.

## Install

### Automatically (recommended)

```bash
sudo ./install.sh
```

This installs the apt packages, the MCC daqhats library, Boost 1.81 and
WiringPi (each skipped if already present), then has `nova-pi.sh` build
novaGround and register the `novaGround@prod` and `novaGround@dev` systemd
services. It asks which Pi this is and writes that to `/etc/nova/pi.env`.
Each environment's broker and backend come from the ops PC
([below](#as-a-service)).

### By hand

Only needed on a machine where `install.sh` can't run.

1. Toolchain and libraries:

   ```bash
   sudo apt install build-essential clang meson ninja-build
   sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev libgpiod-dev libcurl4-openssl-dev
   ```

2. MCC daqhats ([their docs](https://mccdaq.github.io/daqhats/install.html#installation)):

   ```bash
   git clone https://github.com/mccdaq/daqhats.git
   cd daqhats && sudo ./install.sh
   ```

3. Boost 1.81:

   ```bash
   wget https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.bz2
   tar xf boost_1_81_0.tar.bz2 && cd boost_1_81_0
   ./bootstrap.sh --prefix=/usr/local
   ./b2 && sudo ./b2 install
   ```

   Then add to `~/.bashrc`:

   ```bash
   export BOOST_ROOT=/usr/local
   export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
   export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH
   ```

4. WiringPi, for the servo driver:

   ```bash
   git clone https://github.com/WiringPi/WiringPi.git && cd WiringPi
   ./build debian
   sudo apt install ./debian-template/wiringpi_*_arm64.deb
   ```

## Build

meson builds out of the source tree, into `build/`:

```bash
CC=clang CXX=clang++ meson setup build   # once
meson compile -C build                   # every time
```

This builds three executables from the same code:

| Target | Sample | Publish | Use |
|---|---|---|---|
| `novaGround` | 1 ms | 50 ms | The main GSS Pi |
| `novaThermo` | 100 ms | 250 ms | The thermocouple Pi (MCC 134) |
| `novaMock` | 100 ms | 250 ms | The full program on simulated hardware (see [simulation.md](simulation.md)): development and simulator tests |

### The hardware each build expects

Each build knows what its Pi is wired with. It's part of the build, set next to
`NOVA_BUILD_ID` in `meson.build` (`NOVA_EXPECTED_HARDWARE`):

| Build | Expects |
|---|---|
| `novaGround` | GPIO, the DAQ HATs, the TCA9535 relay board (I2C 0x20) and the PCA9685 servo driver (I2C 0x40) |
| `novaThermo` | GPIO and the DAQ HATs (MCC 134) |
| `novaMock` | GPIO, relays and servos, all simulated. Its DAQ HATs come from the scenario, so they aren't expected. |

At startup the program checks each part and logs one line for it, under
`Hardware for <build>:`.

- **GPIO is the only part every build needs.** Without `/dev/gpiochip0` the
  program logs the error and exits with status 1.
- **Any other missing part is logged as an error, and the program carries on
  without it.** The log line starts with `ERROR`, and a
  `HARDWARE MISSING: <parts>` line follows. Commands for that part are refused,
  and its readings are left out.
- **A part that's present but not expected is used, and the log says so.**
- **The I2C chips must answer a register read to count as present.** Selecting
  an I2C address succeeds even when nothing is there. A chip that doesn't
  answer is never read or written again.
- **Repeated I2C errors are logged once, then at most every 10 s.** The repeat
  line says how many were left out. A chip that stops answering can't flood the
  journal.

The result is also written to `$NOVA_STATE_DIR/hardware.json` and is in the
retained `nova/stats/<node>/info` message under `hardware`.
`./nova-pi.sh status` shows it for each environment, in green when everything
expected was found and in red when something wasn't:

```
                 hardware MISSING daq, expected on novaThermo (at 2026-11-24T17:00:00Z)
                          found gpio
```

## Run

An MQTT broker must be reachable (default `localhost:1883`).

```bash
./build/novaMock --broker localhost --verbosity 2
./build/novaGround --broker 192.168.137.1 --backend 192.168.137.1:8000
```

| Option | Default | What |
|---|---|---|
| `--broker <host[:port]\|mqtt://...>` | `localhost:1883` | MQTT broker |
| `--backend <host[:port]\|http://...>` | `http://localhost:8000` | Backend, for uploading data files |
| `--verbosity <0\|1\|2>` | `1` | 0 quiet, 1 info, 2 debug |
| `--sample-ms <ms>` | per target | DAQ sampling interval |
| `--publish-ms <ms>` | per target | Telemetry publish interval |

novaGround has no `--fas-port` option; the FAS link runs in the backend's FAS
bridge. `start.sh` no longer reads `NOVA_FAS_PORT`.

### As a service

There's one systemd instance per environment, like the ops PC's two stacks:

| Unit | Runs | Starts on boot |
|---|---|---|
| `novaGround@prod` | `/home/admin/prod/novaGround` | yes |
| `novaGround@dev` | `/home/admin/dev/novaGround` | no |

Only one runs at a time. Both run `start.sh`, which reads two files:

| File | Holds | Written by |
|---|---|---|
| `/etc/nova/pi.env` | `NOVA_PI_NAME`, `NOVA_TARGET`, and `NOVA_GLOBAL_ARGS` (flags for both environments) | `sudo ./nova-pi.sh install --name <name> --target <binary>`; `configure --global-args` |
| `/etc/nova/novaGround-<env>.env` | the settings below, for that environment | `Nova.ps1` on the ops PC, from `ops/nova.settings.ps1`; or `nova-pi.sh configure` |

The settings in `novaGround-<env>.env`. Empty or missing means the binary's own
default (the file lists the empty ones commented out):

| Setting | Flag it becomes | What it does |
|---|---|---|
| `NOVA_BROKER` | `--broker` | MQTT broker: `host`, `host:port` (default 1883) or a URL. Required. |
| `NOVA_BACKEND` | `--backend` | novaOps backend: `host`, `host:port` (default 8000) or a URL. Required. |
| `NOVA_CONSOLE` | none | Nova Console, for `status`, `report` and `deploy`: `host`, `host:port` (default 8080) or a URL. |
| `NOVA_VERBOSITY` | `--verbosity` | 0 quiet, 1 info, 2 debug. |
| `NOVA_SAMPLE_MS` | `--sample-ms` | DAQ sampling interval. |
| `NOVA_PUBLISH_MS` | `--publish-ms` | Telemetry publish interval. |
| `NOVA_STATS` | `--stats` | Runtime instrumentation, `on` or `off`. |
| `NOVA_STATS_MS` | `--stats-ms` | How often the stats are published; see [instrumentation.md](instrumentation.md). |
| `NOVA_BENCH_FLAGS` | none | `on` lets `start.sh` and `run.sh` pass bench-safety flags (`--stop-actuate`); otherwise they refuse them. Off by default in prod; see [bench-safety.md](bench-safety.md). |
| `NOVA_SIM_SCENARIO` | none | Scenario file for the simulated hardware (novaMock); the binary reads it from its environment. |
| `NOVA_STATE_DIR` | none | Where `last_upload.json` goes. Defaults to `/var/lib/nova/<env>`; the binary reads it from its environment. |
| `NOVA_EXTRA_ARGS` | as written | More flags for this environment only. |

`start.sh` and `run.sh` build the command line the same way (`nova-args.sh`):
the flags from the settings, then `NOVA_GLOBAL_ARGS`, then `NOVA_EXTRA_ARGS`. The
binary takes the last occurrence of a flag, so an environment's
`NOVA_EXTRA_ARGS` beats `NOVA_GLOBAL_ARGS`, and both beat the individual settings.
Values are passed on as written, and the binary reports one it can't read.

Don't edit the `novaGround-<env>.env` files: `Nova.ps1` rewrites them on every
`use`, `deploy` and `get-changes`, which is what keeps prod on prod's backend
port and dev on dev's. If a value is wrong, fix it in `nova.settings.ps1` on the
ops PC. If the ops PC can't reach the Pi, write one by hand:

```bash
sudo ./nova-pi.sh configure --env dev --broker 192.168.137.1:1883 --backend 192.168.137.1:8001
```

`configure` takes a flag for every setting above (`--console`, `--verbosity`,
`--sample-ms`, `--publish-ms`, `--stats`, `--stats-ms`, `--bench-flags`, `--sim-scenario`,
`--state-dir`, `--extra-args`) and changes only the ones you pass, keeping the
rest of that environment's file. `--unset NOVA_STATS` removes one, and
`--global-args "--verbosity 2"` sets `NOVA_GLOBAL_ARGS` in `pi.env`. It restarts
the environment if it is running.

`Nova.ps1` still writes only the broker, backend and console. Until it writes the
rest, a `use`, `deploy` or `get-changes` rewrites the file without the other
settings.

`Nova.ps1 use dev|prod` switches the Pis along with the PC. To switch only this
Pi, use `sudo ./nova-pi.sh use dev` (or `prod`). Manage the services with
`./nova-pi.sh` (`status`, `logs`, `restart`, `doctor`, ...).

At startup novaGround logs its environment and backend, and warns if the
backend's `/health` doesn't answer. Every data-file upload is logged as
`UPLOAD OK` or `UPLOAD FAILED`, and the last result is written to
`/var/lib/nova/<env>/last_upload.json`.

`nova-pi-report.timer` sends `./nova-pi.sh status --json` to the Nova Console
every 15 seconds, so the console shows what each Pi runs.

`./nova-pi.sh metrics --json` prints read-only system metrics: per-core CPU,
load, memory, temperature, `vcgencmd get_throttled`, the running instance's PID,
CPU and RSS, disk I/O and clock sync. It needs no `sudo` and writes nothing, so
it is safe to run over SSH as `admin` while a test is running.

### On the bench

`./run.sh` starts the program in a tmux session instead, so it survives a
dropped SSH connection. It uses dev's settings unless you pass `--env prod`.
Flags after `--` go to the binary and win over the settings, for one run:

```bash
./run.sh --env dev -- --stop-actuate
```

`--stop-actuate` makes the program handle commands without writing any output, for
bench tests that must not move anything. It needs `NOVA_BENCH_FLAGS` on; see
[bench-safety.md](bench-safety.md), which also covers how the program shuts down
on `SIGTERM`.

> [!NOTE]
> `run.sh` and the services can't run at the same time: they'd share the
> hardware and the broker client ID. `run.sh` refuses to start while either
> service is up, and the services refuse while a bench session is running.

### Moving a Pi off the old single service

Pis set up before `novaGround@` had one `novaGround.service` and one
`/etc/nova/novaGround.env`. Running `install` from an up-to-date checkout
replaces them, keeping the target and extra arguments, and carrying the old
broker and backend over as prod's settings:

```bash
cd /home/admin/dev/novaGround && git pull && ./nova-pi.sh build --env dev
sudo ./nova-pi.sh install --name NovaGround --target novaGround   # or NovaThermo / novaThermo
```

Then run `.\ops\Nova.ps1 use prod` on the ops PC to write both environments'
settings.

## Commands, GPIO and thermocouples

The design is `docs/design/pi-io-config.md` in Nova-Collected.

**Topics.** Each Pi takes commands on two topics: `nova/command` for every node
(`data_file`) and `nova/command/<node>` for itself (`relay`, `gpio`, `servo`).
`<node>` is the build's name (`novaGround`, `novaThermo`). A `relay`, `gpio` or
`servo` command that arrives on `nova/command` is refused and logged: every node
would act on it. Retained commands are ignored.

**The per-node config.** The backend publishes a retained message on
`nova/config/<node>` when it connects and whenever the active config changes:
the GPIO outputs (with their limits) and inputs, the relay channels' limits, each
MCC 134 channel's thermocouple type, and the heartbeat timeout. novaGround
applies what it can and answers on the retained `nova/config/<node>/status`:

```json
{
  "config_hash": "3f9a1c0be27d4a55",
  "applied_ms": 1795110000000,
  "rejected": [ { "item": "gpio.outputs.8", "reason": "reserved (SPI0 CE0)" } ],
  "reserved_pins": [0, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 16, 20, 21, 26],
  "thermocouples": { "1": { "0": "K", "1": "T", "2": "K", "3": "K" } },
  "tripped": [ { "output": "gpio:17", "reason": "comms_loss", "time_ms": 1795110123456 } ]
}
```

It rejects only malformed or impossible items, and applies the rest: an unknown
key, a pin outside BCM 0-27, a reserved pin, a pin listed as both input and
output, an unknown thermocouple type, a `daq` address that isn't an MCC 134, and
a GPIO claim or type write that failed. Before any config `config_hash` and
`applied_ms` are null. `thermocouples` lists every channel of every MCC 134 with
the type it is set to.

**GPIO.** No pin is hard-coded. Until the first config arrives novaGround claims
no GPIO pin and refuses every `gpio` command, so the relay hardware's own pull
resistors must hold the relays off during boot. Each configured output is claimed
once at level 0; a pin dropped from the config is driven to 0 and released; a
`gpio` command for a pin that isn't a configured output is refused, as is the old
`mode` field. Failed claims and writes are logged with the pin and libgpiod's
error and counted in the stats (`gpio_claim_failures`, `gpio_write_failures`),
and the actuator CSV records the level really written. A recording's GPIO columns
are the outputs configured when it started.

**Reserved pins.** These are facts about the HATs and the board, so they're in
the code (`src/core/pi_io_config.cpp`) and reserved on every build: 0 and 1 (HAT
ID EEPROM), 2 and 3 (I2C1: relay board and servo driver), 7 to 11 (SPI0, used by
daqhats), 12, 13 and 26 (HAT address select), 16 and 20 (MCC 128 reset and
interrupt), 21 (daqhats' shared interrupt). That leaves 4, 5, 6, 14, 15, 17, 18,
19, 22, 23, 24, 25 and 27; 14 and 15 are the UART.

**Thermocouples.** Every MCC 134 channel starts as type K. A config sets each
configured channel's type (`mcc134_tc_type_write`); a channel with no configured
sensor goes back to K (`kUnconfiguredTcType` in `src/interfaces/analog_io.hpp`;
whether it should be disabled instead is an open question). Only changes are
written.

**Output limits.** See [bench-safety.md](bench-safety.md#output-limits-and-comms-loss):
a maximum on-time and comms-loss trips for relay and GPIO outputs, driven by the
backend's heartbeat on `nova/heartbeat/backend`.

## Watchdogs

Two watchdogs turn the outputs off if novaThermo (or any Pi) freezes:

- **Process watchdog.** `novaGround@.service` is `Type=notify` with
  `WatchdogSec=5`. novaGround sends `READY=1` once its hardware is up (before it
  connects to the broker), then `WATCHDOG=1` from the output supervisor's 50 ms
  tick. If the pings stop for 5 seconds, systemd kills it and `Restart=always`
  starts it again, with the relays de-energized and no GPIO pin claimed. The
  notifications go straight to `$NOTIFY_SOCKET` (no libsystemd); without it, as
  under `run.sh` and in novaMock, nothing is sent.
- **Hardware watchdog.** `sudo ./nova-pi.sh install` adds `dtparam=watchdog=on`
  to the Pi's `config.txt` (`/boot/firmware/config.txt`, or `/boot/config.txt`)
  and sets `RuntimeWatchdogSec=10` in `/etc/systemd/system.conf`, so a hung
  kernel or systemd resets the Pi. Both take effect after a reboot. Running
  `install` again changes nothing. `./nova-pi.sh doctor` and `status` report
  whether both are set and whether systemd is pinging the watchdog.

Neither has been checked on a Pi yet. The bench checks in the design doc (freeze
novaGround with `kill -STOP`, crash the kernel with
`echo c | sudo tee /proc/sysrq-trigger`, with a meter on each relay) still have to
be done, as does checking the Pi 4 watchdog's longest timeout (believed to be
about 15 seconds).

## Data files

During a recording, novaGround writes `<build ID>_<name>_sensors.csv` and
`<build ID>_<name>_actuators.csv` (for example `novaGround_2026-10-04-14_data_0_sensors.csv`)
and uploads them to the backend when the recording stops.

## Stacking DAQ HATs

- Set each board's address jumpers (A0-A2) so addresses count up from 0 in the
  order the boards are installed. **There must always be a board at address 0.**
- After changing the stack, update the stored board information:

  ```bash
  sudo daqhats_read_eeproms
  ```
