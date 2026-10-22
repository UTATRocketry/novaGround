# novaGround

The hardware side of the Nova ground station. It runs on the Raspberry Pi in the
Ground Station Suitcase, reads the MCC DAQ HATs, drives the relay board and
servo driver, publishes telemetry over MQTT and executes the commands the
NovaOps backend sends. C++17, built with meson, run by systemd.

> [!CAUTION]
> This program switches relays and moves servos on real hardware. Test on
> `novaMock` first, and get a lead's review for anything that changes how
> actuators are driven ([CONTRIBUTING.md](CONTRIBUTING.md)).

## Repo structure

```
src/
├── main.cpp          options, wiring, thread start-up
├── interfaces/       hardware drivers: DAQ HATs, GPIO, TCA9535 relays, PCA9685 servos
├── controllers/      device logic and the sampling/publishing loops
├── core/             command router, telemetry store, data logging
└── utils/
docs/setup.md         install, build, run and configure in detail
install.sh            one-time Pi setup
nova-pi.sh            install, use prod|dev, build, status, logs, deploy, doctor, report
start.sh              service entry point (systemd runs this)
run.sh                interactive tmux session, for bench work
novaGround@.service   the systemd unit: novaGround@prod and novaGround@dev
nova-pi-report.*      timer that reports this Pi's state to the Nova Console
```

## Getting started

### Set up

On a Raspberry Pi 4 with **Raspberry Pi OS (Legacy, 64-bit)**:

```bash
sudo chmod +x install.sh
sudo ./install.sh
```

This installs every dependency, builds the code and installs the service.

### Run

```bash
meson compile -C build
./build/novaMock --broker localhost          # no hardware needed
sudo ./nova-pi.sh status                     # the installed service
```

Targets, options, and where the services get their settings:
[docs/setup.md](docs/setup.md).

### Troubleshoot

```bash
sudo ./nova-pi.sh doctor
journalctl -u novaGround@prod -f            # or novaGround@dev
```

| Symptom | Fix |
|---|---|
| Data files never reach the backend | `./nova-pi.sh status` shows each environment's backend and whether it answers; look for `UPLOAD FAILED` in the log. `.\ops\Nova.ps1 use <env>` on the ops PC rewrites the settings |
| `novaGround@<env>` stops with "is running" or "bench session" | Only one of prod, dev and `run.sh` can run. Switch with `sudo ./nova-pi.sh use <env>`, or stop the bench session with `./run.sh --kill` |
| `nova-pi.sh` says the Pi has the old `novaGround.service` | Migrate it: `sudo ./nova-pi.sh install` ([details](docs/setup.md#moving-a-pi-off-the-old-single-service)) |
| A DAQ HAT isn't found | Check the address jumpers, then `sudo daqhats_read_eeproms` |
| Telemetry flickers in the UI | `run.sh` and the service are both running. Stop one |
| Build fails on Debian Trixie | The daqhats library needs Raspberry Pi OS (Legacy, 64-bit) |

More: [Nova SUPPORT.md](https://github.com/UTATRocketry/Nova-Collected/blob/main/SUPPORT.md).

## More information

| To...                            | Read                                                                                                                                                                                                               |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Install, build, run, configure   | [docs/setup.md](docs/setup.md)                                                                                                                                                                                     |
| Set up the Pi on the station     | [Raspberry Pi setup](https://github.com/UTATRocketry/Nova-Collected/blob/main/docs/deployment/novaground-setup.md)                                                                                                 |
| Deploy a release to the Pi       | [RELEASING.md](https://github.com/UTATRocketry/Nova-Collected/blob/main/RELEASING.md)                                                                                                                              |
| Look up MQTT topics and payloads | [MQTT topics](https://github.com/UTATRocketry/Nova-Collected/blob/main/docs/api/mqtt-topics.md)                                                                                                                    |
| Follow code style and layout     | [Style](https://github.com/UTATRocketry/Nova-Collected/blob/main/docs/development/style.md) · [Organization](https://github.com/UTATRocketry/Nova-Collected/blob/main/docs/development/organization.md#novaground) |
| See what changed in each release | [CHANGELOG.md](CHANGELOG.md)                                                                                                                                                                                       |
| Contribute                       | [CONTRIBUTING.md](CONTRIBUTING.md)                                                                                                                                                                                 |
