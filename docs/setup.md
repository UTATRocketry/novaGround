# Setting up and running novaGround

Building, running and configuring novaGround on a Raspberry Pi.

**What this document answers**

- How to install the toolchain and libraries, automatically or by hand
- Which executables the build makes, and how they differ
- Every command-line option, and where the service gets its settings
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
