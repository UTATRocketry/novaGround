# Environment settings

Everything novaGround reads from the environment, who reads it, and what
happens when it is missing. [`.env.example`](../.env.example) is a commented
example of both files.

## How settings reach the program

Settings live in two files on each Pi. Both are plain `KEY=VALUE` lines.

| File | One per | Written by | Section |
|---|---|---|---|
| `/etc/nova/pi.env` | Pi | `sudo ./nova-pi.sh install` (and `configure --global-args`) | [1](#1-etcnovapienv-this-pi) |
| `/etc/nova/novaGround-<env>.env` (`prod`, `dev`) | environment | `Nova.ps1` on the ops PC, from `ops/nova.settings.ps1` | [2](#2-etcnovanovagroundenvenv-this-environment) |

`novaGround@<env>` (systemd) loads `pi.env`, then `novaGround-<env>.env`
(`EnvironmentFile=`), so a key in both files takes the second one. `run.sh`
loads the same two files in the same order by sourcing them in bash.

The settings take two routes to the binary:

```
pi.env + novaGround-<env>.env
        │
        ├─ read by the scripts only ──► choose the binary, check preconditions,
        │                               decide whether bench flags are allowed,
        │                               report to the console
        │
        └─ turned into flags by nova-args.sh (start.sh / run.sh)
                 ──► novaGround --broker ... --backend ... --verbosity ... --sample-ms ...
                     --publish-ms ... --stats ... --stats-ms ... <NOVA_GLOBAL_ARGS> <NOVA_EXTRA_ARGS>

start.sh / run.sh export them to the process ──► NOVA_ENV, NOVA_STATE_DIR, NOVA_SIM_SCENARIO
```

The binary reads no settings from the files. It takes command-line flags and
the three process variables in [section 3](#3-read-by-the-binary-from-its-environment).
It rejects a malformed value but never decides whether a setting is allowed.
Policy, such as refusing `--stop-actuate` in prod, lives in the scripts.

Rules for writing these files:

- Plain `KEY=VALUE`. No `export`, no `$VARIABLE` expansion, no comment after a
  value. systemd does not expand variables and bash would.
- Quote a value with spaces: `NOVA_EXTRA_ARGS="--verbosity 2"`. systemd and
  `nova-pi.sh` strip the quotes. Unquoted, `run.sh` (which sources the file in
  bash) would try to run `2` as a command.
- An empty or missing value leaves the setting to the binary's own default.
  `nova-pi.sh` and `Nova.ps1` write an unset setting commented out.
- There are no default addresses. A missing broker or backend stops the
  service rather than guess (issue #29).

## 1. `/etc/nova/pi.env` (this Pi)

Which Pi this is, which binary it runs, and flags shared by both
environments. `nova-pi.sh install` (`--name`, `--target`) and
`nova-pi.sh configure --global-args` write it. `install` leaves an existing
file alone when its name and target already match. Older files kept the shared
flags in `NOVA_EXTRA_ARGS`; that name now belongs to the per-environment
files, so `install` moves the old value to `NOVA_GLOBAL_ARGS`.

| Variable | Required | Default | Read by | Meaning |
|---|---|---|---|---|
| `NOVA_PI_NAME` | yes | the target with a capital first letter (`novaThermo` → `NovaThermo`) | `nova-pi.sh` (`report`, `doctor`) and `Nova.ps1` (`status`, to match the Pi) | Name shown in the Nova Console. Must match this Pi's name in `ops/nova.settings.ps1`. |
| `NOVA_TARGET` | yes | `novaGround` (the `install` prompt's default) | `start.sh`, `run.sh`, `nova-pi.sh` | Which binary in `build/` to run: `novaGround`, `novaThermo` or `novaMock`. `start.sh` exits 78 if it is empty. |
| `NOVA_GLOBAL_ARGS` | no | empty | `start.sh`, `run.sh` | Extra flags for this Pi's binary in both environments. Set with `nova-pi.sh configure --global-args "<flags>"`. See [flags](#flags-the-binary-accepts) for where it sits in the order. |

## 2. `/etc/nova/novaGround-<env>.env` (this environment)

Where this environment's data goes, and how it runs. `Nova.ps1` rewrites the
file on every `use`, `deploy` and `get-changes`, and it writes every setting
below. Change the value in `ops/nova.settings.ps1` on the ops PC (`Pi.Env.<env>`,
with per-Pi changes in `Pi.Overrides`), not here: anything set by hand with
`nova-pi.sh configure` is overwritten by the next one. Both environments get
the same broker and different backend ports.

To write one by hand when the ops PC can't reach the Pi:

```bash
sudo ./nova-pi.sh configure --env dev --broker 192.168.137.1:1883 --backend 192.168.137.1:8001
```

`configure` changes only the settings you pass and keeps the rest of the file.
`--unset NOVA_<NAME>` removes one. It has a flag for each setting below
(`--verbosity`, `--sample-ms`, `--publish-ms`, `--stats`, `--stats-ms`,
`--bench-flags`, `--sim-scenario`, `--state-dir`, `--extra-args`, ...).

| Variable | Required | Default | Read by | Meaning |
|---|---|---|---|---|
| `NOVA_BROKER` | yes | none | `start.sh`, `run.sh`, `nova-pi.sh` (reporting) | MQTT broker on the ops PC. Passed as `--broker`. |
| `NOVA_BACKEND` | yes | none | `start.sh`, `run.sh`, `nova-pi.sh` (reporting, health check) | novaOps backend for this environment, for data-file uploads. Passed as `--backend`. |
| `NOVA_CONSOLE` | no | empty (set by `Nova.ps1`) | `nova-pi.sh` only (`report`, `doctor`, and release lookup in `update`/`deploy`, from prod's file) | Nova Console URL. Both environments point at **prod's** console, which stays up while dev is live. With none, the Pi can't report or look up a release. `nova-pi.sh configure --console <url>` sets it by hand. |
| `NOVA_VERBOSITY` | no | unset (1 in the binary) | `start.sh`, `run.sh` | Log detail: 0 quiet, 1 info, 2 debug. Passed as `--verbosity`. |
| `NOVA_SAMPLE_MS` | no | unset (per target, below) | `start.sh`, `run.sh` | DAQ sampling interval in ms. Passed as `--sample-ms`. |
| `NOVA_PUBLISH_MS` | no | unset (per target, below) | `start.sh`, `run.sh` | Telemetry publish interval in ms. Passed as `--publish-ms`. |
| `NOVA_STATS` | no | unset (off in the binary) | `start.sh`, `run.sh` | Runtime instrumentation, `on` or `off`. Passed as `--stats`. `Nova.ps1` writes `off` for prod and `on` for dev by default. |
| `NOVA_STATS_MS` | no | unset (1000 in the binary) | `start.sh`, `run.sh` | How often stats are published, in ms. Passed as `--stats-ms`. |
| `NOVA_BENCH_FLAGS` | no | unset (**off**) | `start.sh`, `run.sh` (`check_bench_flags`); never the binary | Whether bench-safety flags such as `--stop-actuate` may be used. `on`, `1`, `true` or `yes` (any case) allow them; anything else, including unset, refuses. `Nova.ps1` writes `off` for prod and `on` for dev by default. See [Bench-safety flags](#bench-safety-flags). |
| `NOVA_SIM_SCENARIO` | no | unset (a default scenario) | the binary (`novaMock`'s simulated DAQ library), via `start.sh` / `run.sh`, which export it | Path to a scenario JSON file for the simulated hardware. A file that can't be read makes the simulator fail. Only the `novaMock` build reads it. See [simulation.md](simulation.md). |
| `NOVA_STATE_DIR` | no | `/var/lib/nova/<env>` | the binary, via `start.sh` / `run.sh`, which export it | Where it writes `last_upload.json` after each upload, which `nova-pi.sh` reports to the console. |
| `NOVA_EXTRA_ARGS` | no | empty | `start.sh`, `run.sh` | Extra flags for this environment only, added **last**, so they override every other source. See [flags](#flags-the-binary-accepts). |

`NOVA_STATS` and `NOVA_STATS_MS` are passed on as written. If the binary
can't read one, it says so and ignores it (see [instrumentation.md](instrumentation.md)).
A malformed value for any other setting makes the binary exit.

### Address forms

The binary normalises what it is given:

| Setting | Accepts | Bare host becomes |
|---|---|---|
| `NOVA_BROKER` | `host`, `host:port`, `mqtt://host:port` | `mqtt://host:1883` |
| `NOVA_BACKEND` | `host`, `host:port`, `http://host:port` | `http://host:8000`; a trailing `/` is dropped, and uploads go to `<backend>/api/data-files/upload` |

## 3. Read by the binary from its environment

Don't put `NOVA_ENV` in either file: the scripts set it. `NOVA_STATE_DIR` and
`NOVA_SIM_SCENARIO` are the file settings above, which `start.sh` and `run.sh`
export (`run.sh` puts them on the tmux command line, because tmux starts a
fresh shell).

| Variable | Value | Set by | Read by | Meaning |
|---|---|---|---|---|
| `NOVA_ENV` | `prod` or `dev` | `novaGround@.service` (from `%i`); `run.sh` (from `--env`) | `start.sh` (must be `prod` or `dev`, else exit 78); the binary logs it at startup | Which environment this process is. |
| `NOVA_STATE_DIR` | `/var/lib/nova/<env>` unless the file sets another | the unit (and `StateDirectory=`); `start.sh`; `run.sh` | the binary | See above. Unset: nothing is written. |
| `NOVA_SIM_SCENARIO` | a file path | `start.sh`, `run.sh`, from the file | the `novaMock` build's simulated DAQ HATs | See above. |

## Flags the binary accepts

These are the only inputs the binary takes from the command line.

| Flag | Fed by | Default |
|---|---|---|
| `--broker <host[:port]\|mqtt://...>` | `NOVA_BROKER` | `localhost:1883` (compiled in) |
| `--backend <host[:port]\|http://...>` | `NOVA_BACKEND` | `http://localhost:8000` (compiled in) |
| `--verbosity <0\|1\|2>` | `NOVA_VERBOSITY` | 1 (0 quiet, 1 info, 2 debug) |
| `--sample-ms <ms>` | `NOVA_SAMPLE_MS` | per target: novaGround 1, novaThermo 100, novaMock 100 |
| `--publish-ms <ms>` | `NOVA_PUBLISH_MS` | per target: novaGround 50, novaThermo 250, novaMock 250 |
| `--stats <on\|off>` | `NOVA_STATS` | off. `on`/`1`/`true`/`yes` and `off`/`0`/`false`/`no`. |
| `--stats-ms <ms>` | `NOVA_STATS_MS` | 1000 |
| `--stop-actuate` | `NOVA_GLOBAL_ARGS`, `NOVA_EXTRA_ARGS`, or `run.sh ... -- --stop-actuate`; allowed only with `NOVA_BENCH_FLAGS` on | off |

The compiled-in broker and backend are never used on a Pi: the scripts refuse
to start without the settings. A bad `--verbosity`, `--sample-ms` or
`--publish-ms` makes the binary exit; a bad `--stats` or `--stats-ms` is
reported and ignored.

Order on the command line, built by `nova-args.sh`:

1. `--broker`, `--backend`, then the flags from the individual settings
   (`NOVA_VERBOSITY`, `NOVA_SAMPLE_MS`, `NOVA_PUBLISH_MS`, `NOVA_STATS`,
   `NOVA_STATS_MS`)
2. `NOVA_GLOBAL_ARGS` (from `pi.env`)
3. `NOVA_EXTRA_ARGS` (from this environment's file)
4. `run.sh` only: the flags after `--`, for that one run

The last occurrence of a flag wins, so each step overrides the ones before it.
Both scripts split `NOVA_GLOBAL_ARGS` and `NOVA_EXTRA_ARGS` on whitespace, so
quotes inside them aren't honoured. `run.sh` then passes the list through a
tmux shell.

## Bench-safety flags

`--stop-actuate` makes novaGround receive, route and time commands but drop
every write to an output driver (see [bench-safety.md](bench-safety.md)). The
binary accepts it in any environment. `start.sh` and `run.sh` decide whether
it is allowed. They check the finished command line, so the flag is caught
whichever way it got there: `NOVA_GLOBAL_ARGS`, `NOVA_EXTRA_ARGS` or the
arguments after `run.sh --`.

With `NOVA_BENCH_FLAGS` off, a bench-safety flag stops the instance from
starting (`start.sh` exits 78, `run.sh` exits 1) rather than starting it with
outputs disabled. Failing loudly is deliberate. A leftover flag in prod's file
would otherwise start prod normally, with nothing to show it can't move a
valve. To allow the flag on a Pi:

```bash
sudo ./nova-pi.sh configure --env dev --bench-flags on
```

Or, for a Pi that `Nova.ps1` manages, set `BenchFlags` in `ops/nova.settings.ps1`.
It defaults to `off` for prod and `on` for dev.

## Used by the scripts only

These never reach the binary.

| Variable | Used by | Meaning |
|---|---|---|
| `NOVA_PI_NAME`, `NOVA_CONSOLE` | `nova-pi.sh` | See sections 1 and 2. |
| `NOVA_TARGET` | scripts (to pick `build/<target>`) | Not passed to the binary. |
| `NOVA_BENCH_FLAGS` | `nova-args.sh` | See [Bench-safety flags](#bench-safety-flags). |
| `NOVA_PI_ROOT` | `nova-pi.sh`, `run.sh` | **Tests only.** Prefixes every path (`/etc/nova`, `/home/admin`, ...) so a test runs in a scratch directory. Empty on a Pi. Don't set it in a settings file. |

`nova-pi.sh metrics --json` takes `PROC_ROOT` and `SYS_ROOT` (default `/proc`
and `/sys`) for tests in the same way. Don't set them on a Pi.

## Which script checks what

| Condition | `start.sh` | `run.sh` |
|---|---|---|
| `NOVA_ENV` is `prod` or `dev` | required, else exit 78 | n/a: the environment comes from `--env` (default `dev`) |
| `NOVA_TARGET` set | required, else exit 78 | required, or `--target` |
| `NOVA_BROKER` and `NOVA_BACKEND` set | both required, else exit 78 | both required, or `--broker` / `--backend` |
| The other environment, or a bench session, running | exit 78 | refuses if any `novaGround@<env>` service is running |
| A bench-safety flag without `NOVA_BENCH_FLAGS` on | exit 78 | exit 1 |
| Binary built | exit 1 (systemd keeps retrying) | exit 1 |

Exit 78 is a problem a restart can't fix; the unit's
`RestartPreventExitStatus=78` stops it looping.
