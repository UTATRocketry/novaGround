# Runtime instrumentation (`NOVA_STATS`)

Timing data for benchmarking and bench tests, without attaching a profiler.
One setting switches all of it:

| `NOVA_STATS` | Effect |
|---|---|
| unset, empty, `0`, `false`, `off`, `no` | Off. Nothing below happens; telemetry is exactly as before. |
| `1`, `true`, `on`, `yes` | On. |
| anything else | Warned about at startup, and treated as off. A typo in a diagnostics setting must never keep the program from commanding valves. |

It is read from the environment, so it belongs in the Pi's env file
(`/etc/nova/novaGround-<env>.env`), which `Nova.ps1` writes from
`ops/nova.settings.ps1`. The binary's default is off. The per-environment
defaults (on in dev, off in prod) are the ops settings' job, not the code's.
`run.sh` passes it on to the bench session; `start.sh` gets it from the unit's
`EnvironmentFile`.

## Telemetry fields

With stats on, each `nova/telemetry/engine` packet gains three fields. They are
additive; the backend ignores fields it does not know.

| Field | Meaning |
|---|---|
| `seq` | Per-node packet counter, starting at 1 for each run. Gaps are loss, repeats or going backwards are duplication or reordering. |
| `sample_ms` | When the newest sensor sample in the packet was read (system clock, ms since the epoch). Absent before the first sample. |
| `publish_ms` | When the packet was built (system clock, ms since the epoch). |

`publish_ms - sample_ms` is the packet's age at publish. Compare `sample_ms`
across hops for staleness. Across machines it is only as good as their clock
sync.

## The stats topic

`nova/stats/<node>` (the node is `novaGround`, `novaThermo` or `novaMock`),
once a second, only with stats on. Each message summarises the second since the
previous one:

```json
{
  "node": "novaGround", "env": "dev", "build_id": "novaGround",
  "git_commit": "7c19b27a1b2c", "actuation": "enabled",
  "seq": 42, "time_ms": 1793000000000, "uptime_s": 41.9,
  "loops": {
    "sampler": { "configured_ms": 1, "ticks": 41800, "overruns": 3,
                 "period_ms": { "count": 1000, "min": 1.0, "p50": 1.1, "p99": 1.9, "max": 2.6 } }
  },
  "commands": {
    "relay": { "count": 5,
               "received_to_handled_ms": { "count": 1, "min": 0.1, "p50": 0.1, "p99": 0.1, "max": 0.1 },
               "received_to_done_ms":    { "count": 1, "min": 0.8, "p50": 0.8, "p99": 0.8, "max": 0.8 } }
  },
  "publish_failures": 0,
  "daq_read_errors": 0
}
```

- **`loops`**: `sampler`, `publisher`, `consumer` and `gpio_sampler`. The period
  is the time between passes. A period more than 1.5 times the configured one is
  an **overrun**. `consumer` waits up to 100 ms for a message, so its
  configured period is that poll time. `ticks` and `overruns` count since the
  start; the `period_ms` percentiles cover only the last second (and at most
  10,000 passes).
- **`commands`**: one entry per command type that reached a handler (so a
  wrong `source`, bad JSON or an unknown type are not counted). `count` is
  since the start; the two latencies cover the last second.
  - `received_to_handled_ms`: the message was consumed -> the handler is about to run (parse and routing).
  - `received_to_done_ms`: the message was consumed -> the handler returned,
    which is after the driver write. Commands rejected by a controller (bad
    id, hardware unavailable) are counted too: they did reach their handler.
- **`publish_failures`**: telemetry publishes that threw. **`daq_read_errors`**:
  channel reads that failed (published as NaN).
- **`actuation`** is always `enabled` for now; part D's `--no-actuate` and
  `--only-actuators` change it.

Not yet reported (planned for the rest of part C): the consumer's queue depth
(paho does not expose it), and actuator state read back from the drivers.

Intervals are measured with `steady_clock`; only `time_ms`, `sample_ms` and
`publish_ms`, which leave the process, use the system clock.
