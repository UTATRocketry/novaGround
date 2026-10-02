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
novaGround and register the `novaGround` systemd service. Settings are written
to `/etc/nova/novaGround.env`.

### By hand

Only needed on a machine where `install.sh` can't run.

1. Toolchain and libraries:

   ```bash
   sudo apt install build-essential clang meson ninja-build
   sudo apt install libpaho-mqtt-dev libgpiod-dev libcurl4-openssl-dev
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
   mv debian-template/wiringpi_3.18_arm64.deb .
   sudo apt install ./wiringpi_3.18_arm64.deb
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
| `novaMock` | 100 ms | 250 ms | No hardware initialisation: development and simulator tests |

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

> [!WARNING]
> novaGround has no `--fas-port` option; the FAS link runs in the backend's FAS
> bridge. `start.sh` still passes one if `NOVA_FAS_PORT` is set, which makes the
> service exit in a loop. Keep `NOVA_FAS_PORT` empty.

### As a service

The `novaGround` systemd service runs `start.sh`, which reads
`/etc/nova/novaGround.env`:

| Variable | Default |
|---|---|
| `NOVA_TARGET` | `novaGround` |
| `NOVA_BROKER` | `192.168.137.1` |
| `NOVA_BACKEND` | `192.168.137.1:8000` |
| `NOVA_EXTRA_ARGS` | empty (extra command-line options) |

After editing it: `sudo systemctl restart novaGround`. Manage the service with
`./nova-pi.sh` (`status`, `logs`, `restart`, `doctor`, ...).

For bench work, `./run.sh` starts the program in a tmux session instead, so it
survives a dropped SSH connection.

> [!NOTE]
> `run.sh` and the service can't run at the same time: they'd share the
> broker client ID. `run.sh` warns if the service is up.

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
