# Nova Ground

Ground-station data acquisition and actuator control daemon for the Nova rocket system. Runs on a Raspberry Pi 4, samples MCC DAQ HAT ADC channels, drives servos/relays/GPIO, bridges the FAS (Flight Actuation System) RS-422 serial link, and exchanges sensor telemetry and commands with the novaOps backend over MQTT.

> **Platform requirement:** Raspberry Pi 4 running Raspberry Pi OS (Legacy, 64-bit). The MCC DAQHat library does not support Debian Trixie.

---

## Build targets

Three executables are built from a single source tree, differentiated by compile-time defaults:

| Target | Build ID | Sample interval | Publish interval | Notes |
|--------|----------|----------------|-----------------|-------|
| `novaGround` | `novaGround` | 1 ms | 50 ms | Full hardware |
| `novaThermo` | `novaThermo` | 100 ms | 250 ms | Thermocouple variant |
| `novaMock` | `novaMock` | 100 ms | 250 ms | Hardware init disabled |

---

## Dependencies

### Build tools

```bash
sudo apt install build-essential clang meson
```

### Boost (JSON + thread)

```bash
wget https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.bz2
tar xf boost_1_81_0.tar.bz2
cd boost_1_81_0
./bootstrap.sh --prefix=/usr/local
./b2
sudo ./b2 install

echo 'export BOOST_ROOT=/usr/local' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
echo 'export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH' >> ~/.bashrc
```

### Other libraries

```bash
sudo apt-get install libpaho-mqtt-dev libgpiod-dev libcurl4-openssl-dev
```

### MCC DAQHats

```bash
git clone https://github.com/mccdaq/daqhats.git
cd daqhats
sudo ./install.sh
```

[DAQHat documentation](https://mccdaq.github.io/daqhats/install.html#installation)

### WiringPi (servo driver)

```bash
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build debian
mv debian-template/wiringpi_3.18_arm64.deb .
sudo apt install ./wiringpi_3.18_arm64.deb
```

---

## Building

```bash
cd novaGround
CC=clang CXX=clang++ meson setup build
meson compile -C build
```

---

## Running

An MQTT broker must be reachable (defaults to `localhost:1883`).

```bash
./build/novaGround
./build/novaThermo
./build/novaMock
```

### Runtime options

| Flag | Default | Description |
|------|---------|-------------|
| `--broker <host[:port]\|mqtt://...>` | `localhost:1883` | MQTT broker address |
| `--backend <host[:port]\|http://...>` | `http://localhost:8000` | Backend base URL for data-file uploads |
| `--verbosity <0\|1\|2>` | `1` | 0 = quiet, 1 = info, 2 = debug |
| `--sample-ms <ms>` | target default | DAQ sampling interval |
| `--publish-ms <ms>` | target default | Telemetry publish interval |
| `--fas-port <device>` | *(disabled)* | FAS RS-422 serial port, e.g. `/dev/ttyUSB0` |
| `--fas-baud <baud>` | `460800` | FAS serial baud rate |

Examples:

```bash
./build/novaGround --publish-ms 250 --verbosity 2
./build/novaGround --fas-port /dev/ttyUSB0
./build/novaThermo --broker 192.168.0.1 --backend 192.168.0.1:8000
```

---

## FAS integration

novaGround can maintain a direct RS-422 serial connection to the FAS FMC bridge. When `--fas-port` is supplied the binary:

- Decodes the FAS wire protocol (`[0xAA][LEN_LO][LEN_HI][CAN_ID 4B LE][data][CRC16]`)
- Forwards ADC samples from EPB boards into the telemetry sensor stream at `hat_id = 100 + board_id`
- Publishes FAS board online/offline status and IMC arm state in every telemetry frame
- Accepts FAS actuator commands from the novaOps backend (both `board_type`/`port`/`action` shape and direct `op` shape)
- Sends periodic `DISCOVERY_REQ` frames and marks boards offline after 3 s without a heartbeat
- Forwards raw decoded frames to `nova/console` when console mode is active

FAS support is fully optional — omitting `--fas-port` leaves all other functionality unchanged.

### Console mode

Send `{"type":"console","action":"start"}` on `nova/command` to begin streaming raw FAS frame JSON to `nova/console`. Each message looks like:

```json
{
  "source": "novaGround",
  "type": "fas_frame",
  "can_id": 524544,
  "msg_type": 1,
  "board_kind": 2,
  "board_id": 0,
  "channel": 0,
  "data_hex": "a0 86 01 00 00 00 00 00"
}
```

Send `{"type":"console","action":"stop"}` to stop.

---

## MQTT

| Direction | Topic | Payload |
|-----------|-------|---------|
| Publish | `nova/telemetry` | `{"source":"novaGround","sensors":[…],"gpios":[…],"fas_boards":[…],"fas_imc":{…}}` |
| Subscribe | `nova/command` | `{"source":"novaOps","command":{"type":"…",…}}` |
| Publish (console) | `nova/console` | Raw FAS frame JSON (when console mode active) |

### Command types handled

| `type` | Description |
|--------|-------------|
| `servo` | Set servo channel angle/pulse |
| `relay` | Set relay channel on/off |
| `gpio` | Set GPIO output pin state |
| `fas` | FAS actuator command (see below) |
| `console` | Start/stop raw FAS frame output |
| `data_file` | Data logging control |

### FAS command shapes

Both shapes are accepted simultaneously:

```jsonc
// novaOps high-level shape
{"type":"fas","board_type":"EPB","board_id":0,"port":"relay","channel":2,"action":"on"}
{"type":"fas","board_type":"EPB","board_id":0,"port":"servo","channel":1,"value":1500}
{"type":"fas","board_type":"EPB","board_id":0,"port":"servo","channel":1,"value":0}
{"type":"fas","board_type":"EPB","board_id":0,"port":"gpio","channel":0,"action":"ARM"}

// Direct op shape
{"type":"fas","op":"pwm_set","board_id":0,"channel":1,"pulse_us":1500,"period_us":20000}
{"type":"fas","op":"load_sw_set","board_id":0,"channel":2,"enable":true}
{"type":"fas","op":"imc_arm","board_id":0}
{"type":"fas","op":"imc_disarm","board_id":0}
{"type":"fas","op":"failsafe","board_id":0}
{"type":"fas","op":"discover"}
{"type":"fas","op":"actuator_query","board_id":0,"channel":1}
```

---

## Service and autostart

The `novaGround.service` systemd unit and `run.sh` tmux launcher are provided for deployment.

To enable FAS at boot, uncomment and set `FAS_PORT` in the service file:

```ini
# novaGround.service
Environment="FAS_PORT=/dev/ttyUSB0"
```

Or export it before calling `run.sh` directly:

```bash
FAS_PORT=/dev/ttyUSB0 ./run.sh
```

`FAS_BAUD` can also be set this way if a non-default baud rate is needed.

Install the service:

```bash
sudo cp novaGround.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable novaGround
sudo systemctl start novaGround
```

---

## Data logging

CSV files are written to the `data/` directory and prefixed with the build ID:

```
novaGround_<session>_sensors.csv
novaGround_<session>_actuators.csv
```

---

## Hardware setup

When stacking multiple MCC DAQ HAT boards, install address jumpers on A0–A2 starting at 0. There must always be a board at address 0. After changing the stack:

```bash
sudo daqhats_read_eeproms
```
