# Runtime instrumentation (`NOVA_STATS`)

Timing data for benchmarking and bench tests, without attaching a profiler.

| Setting | Effect |
|---|---|
| `NOVA_STATS` | `1`, `true`, `on`, `yes` turn everything below on. Unset, empty, `0`, `false`, `off`, `no` leave it off, and nothing below happens: telemetry is exactly as before. |
| `NOVA_STATS_INTERVAL_MS` | How often the stats message is published, in whole milliseconds. Default 1000. |

Both are read from the environment, so they belong in the Pi's env file
(`/etc/nova/novaGround-<env>.env`), which `Nova.ps1` writes from
`ops/nova.settings.ps1`. The binary's defaults are off and 1000 ms. The
per-environment defaults (on in dev, off in prod) are the ops settings' job, not
the code's. `run.sh` passes both on to the bench session; `start.sh` gets them
from the unit's `EnvironmentFile`.

A value the program cannot read (`NOVA_STATS=maybe`, `NOVA_STATS_INTERVAL_MS=0`
or `1.5`) is warned about at startup and ignored: stats stay off, or the interval
stays 1000. A typo in a diagnostics setting must never keep the program from
commanding valves.

## Where things are published

Everything is under `nova/stats/<node>`, where `<node>` is `novaGround`,
`novaThermo` or `novaMock`. A subscriber to `nova/stats/#` gets every node; the
**topic** says which one, so no payload repeats it.

| Topic | Retained | Rate | Holds |
|---|---|---|---|
| `nova/stats/<node>/info` | yes | on every (re)connect | what this process is and how to read its stats |
| `nova/stats/<node>` | no | every `NOVA_STATS_INTERVAL_MS` | measurements only |

Telemetry (`nova/telemetry/engine`) also gains three fields, described last.

### The info message (retained)

Published (QoS 1, retained) each time the client connects or reconnects, so a
broker restart that dropped it gets it back. A late subscriber receives it as
soon as it subscribes. It describes the process, not the last second:

```json
{
  "schema": 1,
  "build_id": "novaGround",
  "started_ms": 1793000000000,
  "stats_interval_ms": 1000,
  "loops": {
    "gpio_sampler": { "configured_ms": 50 },
    "publisher":    { "configured_ms": 50 },
    "sampler":      { "configured_ms": 1 }
  },
  "measurement": { "overrun_period_factor": 2.0, "buckets_per_octave": 16,
                   "lowest_ms": 0.001, "relative_error": 0.0443 }
}
```

- **No settings yet.** The message deliberately does not list the environment,
  the commit the binary was built from, or the actuation mode. The Pi report
  already says which environment is running, and the others have no use yet.
  It could grow into a record of every setting the program runs with.
- **`started_ms`** (system clock) tells a new run from an old retained message:
  the broker keeps the last info message after the process dies, and a run with
  `NOVA_STATS` off publishes none and does not clear it.
- **`loops`** lists the loops and their configured periods, so the stats message
  does not repeat them. `consumer` is absent on purpose: it waits on the broker
  and has no period of its own. The per-command latency covers it.

### The stats message (not retained)

```json
{
  "seq": 42, "time_ms": 1793000041900, "uptime_s": 41.9,
  "loops": {
    "sampler": { "ticks": 41800, "overruns": 3,
                 "period_ms": { "last":  { "count": 1000, "min": 1.0, "p50": 1.1, "p99": 1.9, "max": 2.6 },
                                "total": { "count": 41799, "min": 1.0, "p50": 1.1, "p99": 2.0, "max": 9.4 } } }
  },
  "commands": {
    "relay": { "count": 5,
               "received_to_handled_ms": { "last": { "...": 0 }, "total": { "...": 0 } },
               "received_to_done_ms":    { "last": { "...": 0 }, "total": { "...": 0 } } }
  },
  "publish_failures": 0,
  "daq_read_errors": 0
}
```

- **`seq`** counts stats messages, so a gap is a lost message.
- **`last` and `total`.** Every distribution is given over two spans: `last`
  covers the time since the previous stats message, and `total` covers the whole
  run. A loop running every 100 ms puts only about ten values in a one-second
  `last`, so its p99 is just its max; its `total` keeps meaningful percentiles.
  `count` is the number of values in the span.
- **Percentiles** come from fixed log-spaced buckets (16 per octave, 1 µs to
  about 4.5 minutes), so they are never more than about 4.4% above the true
  value (`relative_error`) and are clamped to the exact `min` and `max`. Memory
  and cost per value are constant at any rate.
- **`loops.<name>.period_ms`** is the time between passes of the loop.
  **`overruns`** counts passes that took at least `overrun_period_factor` (2×)
  the configured period, i.e. a whole tick was lost. The loops sleep for their
  interval after each pass, so a healthy period is a little over the
  configured one. There is no other threshold: the histogram is there for
  anyone who wants one.
- **`commands`** has one entry per command type that reached a handler (a wrong
  `source`, bad JSON or an unknown type is not counted). `count` is since the
  start.
  - `received_to_handled_ms`: the message was consumed -> the handler is about to run (parse and routing).
  - `received_to_done_ms`: the message was consumed -> the handler returned,
    which is after the driver write. Commands that a controller rejects (bad id,
    hardware unavailable) are counted too: they did reach their handler.
- **`publish_failures`**: telemetry publishes that threw. **`daq_read_errors`**:
  channel reads that failed (published as NaN).

## Telemetry fields

With stats on, each `nova/telemetry/engine` packet gains three fields. They are
additive; the backend ignores fields it does not know.

| Field | Meaning |
|---|---|
| `seq` | Per-node packet counter, starting at 1 for each run. A gap is loss; a repeat or a step back is duplication or reordering. |
| `sample_ms` | When the newest sensor sample in the packet was read (system clock, ms since the epoch). Absent before the first sample. |
| `publish_ms` | When the packet was built (system clock, ms since the epoch). |

`publish_ms - sample_ms` is the packet's age at publish. Across machines,
timestamps are only as good as the clock sync.

## Not done yet

The consumer's queue depth (paho does not expose it) and actuator state read back
from the drivers. Intervals use `steady_clock`; only `time_ms`, `started_ms`,
`sample_ms` and `publish_ms`, which leave the process, use the system clock.
