# Bench-safety modes

For bench tests on a real Pi that must not move an output: measuring the command
path, or rehearsing a procedure, with the valves and relays left alone.

## `--stop-actuate`

```bash
./run.sh --env dev -- --stop-actuate
```

Commands are received, parsed, validated, routed, timed and logged, and the
controllers update their state, telemetry and recordings exactly as normal. The
only difference is at the bottom: every write to an output driver is dropped and
reported as successful.

| Dropped | Still real |
|---|---|
| Relay writes (TCA9535 output registers) | Reads of the relay registers |
| Servo pulse and disable writes (PCA9685) | Reads of the servo channels |
| GPIO writes and output claims | GPIO input claims, releases, input and output-line reads, the GPIO sampler |
| Output-supervisor trips (they go through the same wrappers) | The supervisor's timers and the `tripped` list in the config status |
| | The DAQ, telemetry, data files and uploads |
| | Command validation, timing, the stats |

Because the wrappers sit under the controllers (`src/core/actuation_guard.hpp`), no
controller has a stop branch, and a write added to a controller later is dropped
without anyone remembering to.

**What it does not stop.** The relays are still set to their startup state, as
they always are: every relay de-energized (bit 1; the boards are active-low).
Starting novaGround with `--stop-actuate` on a Pi whose valves are wired to the
relays moves them exactly as starting it without. Start it with the plant in a
safe state. No GPIO pin is claimed at startup in either mode, and under
`--stop-actuate` the config's GPIO outputs are never claimed either (the claim is
dropped), so their lines stay as the hardware's own pulls hold them.

`--stop-actuate` doesn't affect the systemd or hardware watchdogs (see
[setup.md](setup.md#watchdogs)); under it, the outputs don't move anyway.

### How to tell it is on

- The binary prints `Actuation: STOPPED (--stop-actuate)` on stderr at startup,
  whatever `--verbosity` is.
- The retained `nova/stats/<node>/info` message has `"actuation": "stopped"`
  (`"enabled"` otherwise). It is published on every connect. It needs
  `NOVA_STATS` on, like everything under `nova/stats`.
- Every stats message has `stopped_writes`, the number of writes dropped since the
  start. It rises with each command that would have driven an output.
- `nova/state/<node>` still reports what the hardware holds, not what was
  commanded, so after a relay command it shows the relay unchanged. That is the
  check that nothing moved. Telemetry and the recordings show the commanded state.

## Who is allowed to use it

The binary accepts the flag in every environment: compiled code does not decide
what a setting allows. `start.sh` and `run.sh` do, through `NOVA_BENCH_FLAGS` in
the environment's file (`/etc/nova/novaGround-<env>.env`):

| `NOVA_BENCH_FLAGS` | `--stop-actuate` |
|---|---|
| `on` (also `1`, `true`, `yes`) | passed to the binary |
| anything else, or unset | refused: `run.sh` exits 1, `start.sh` exits 78 (systemd does not retry) |

The check looks at the finished command line, so it covers every way the flag can
arrive: `NOVA_GLOBAL_ARGS`, `NOVA_EXTRA_ARGS`, or the arguments after `--`. The
setting is off by default in prod and on in dev; the ops PC's settings write it,
and `sudo ./nova-pi.sh configure --env <env> --bench-flags on|off` writes it by
hand. Turning it on is a station change an operator makes, never a benchmark
script's.

Leaving the flag on by mistake in prod would stop prod from driving valves,
which is its own hazard. That is why prod refuses it by default, and why a
refused flag stops the instance from starting instead of being ignored.

## `run.sh -- <args>`

Everything after `--` goes to the binary as written, after the settings and
`NOVA_GLOBAL_ARGS` and `NOVA_EXTRA_ARGS`, so it wins over them:

```bash
./run.sh --env dev -- --stop-actuate --stats on --stats-ms 250
```

If the bench session already exists, `run.sh` refuses flags after `--` instead of
attaching to a session that was started without them. Stop it first with
`./run.sh --kill`.

## Shutdown on `SIGTERM`, `SIGINT` and `SIGHUP`

`systemctl stop` (`SIGTERM`), Ctrl+C (`SIGINT`) and `./run.sh --kill`, which makes
tmux hang up on the program (`SIGHUP`), now end it in order:

1. The loops stop and are joined (publisher, samplers, consumer, output
   supervisor, stats).
2. With stats on, the last stats window is written to the log (`Final stats:`)
   and published.
3. The data recording is closed, so no file is left half written.
4. The MQTT client disconnects.
5. It logs `Shutdown complete.` and exits 0.

Shutdown writes no output. Relays, servos and GPIO stay as they were, which is
the safe state for a process that is only restarting. Nothing else holds the
outputs, so a restart sets them up again as at any startup: relays
de-energized, GPIO pins unclaimed until the config arrives (a released line keeps
the level it was last driven to until then; the relay hardware's pull resistors
decide what an unclaimed pin does after a reboot).

## Output limits and comms loss

These run in every mode, bench or not. The per-node config
(`nova/config/<node>`) can give each relay and GPIO output a maximum on-time
(`max_on_ms`) and `on_comms_loss: safe`. The output supervisor checks every 50 ms:

- An output on longer than its `max_on_ms` since it last left its safe level
  goes back to it (reason `max_on_time`).
- When no live `{"online":true}` heartbeat has arrived on
  `nova/heartbeat/backend` within `heartbeat.timeout_ms`, or the backend's last
  will (`{"online":false}`) or an MQTT connection loss is seen, every `safe`
  output goes back to its safe level (reason `comms_loss`), and a command that
  would move one off it is refused until a live heartbeat arrives. Retained
  heartbeats don't count.
- A trip latches: the output stays at its safe level until the next command for
  it, and is listed in `tripped` in the retained `nova/config/<node>/status`.

Safe is a GPIO level of 0 and a de-energized relay. Comms-loss supervision starts
only once a config with a `heartbeat` section has arrived. A bench run without a
backend that sends heartbeats should use a config without one, or publish the
beats itself.

## Not done yet

`--only-actuators`, which would restrict a run that does drive outputs to a list
of channels, is not implemented. A bench run that must drive an output relies on
the benchmark scripts' allow-list for now.
