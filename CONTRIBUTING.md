# Contributing to novaGround

**The rules for all Nova repos are in
[Nova-Collected/CONTRIBUTING.md](https://github.com/UTATRocketry/Nova-Collected/blob/main/CONTRIBUTING.md).
Read that first.** In short:

- `dev` collects finished work for hardware testing; `main` is what prod runs.
- Branch from `dev` as `feature/...`, `bugfix/...` or `chore/...`, e.g. `bugfix/fas-port-reconnect`.
- Open a PR **into `dev`**, titled `type(scope): what changed`, and fill in the template.
- CI green + 1 approval (a lead's if it can move hardware), then **Squash and merge**.
- Leads bring tested releases into `main`; you never merge into `main` yourself.

This file only covers what is specific to this repo.

## Before you open a PR

On a Pi (or any Linux box with the dependencies from the README):

```bash
CC=clang CXX=clang++ meson setup build   # once
meson compile -C build
```

CI can't build without the HAT libraries, so it only checks the shell scripts
and line endings. **Say in the PR that it compiled, and where.**

## Simulator Test (level 2)

```bash
./run.sh --target novaMock
```

`novaMock` runs the full program against simulated boards. `run.sh` warns if
the systemd service is running; the two can't share the serial port or broker
client ID.

## Scopes for PR titles

`daq`, `servo`, `relay`, `gpio`, `mqtt`, `fas`, `logger`, `pi` (scripts and
service)

## Things to know

- Everything under `src/` can move hardware, so every PR touching it needs a
  code owner's approval.
- `*.sh` and `*.service` must keep LF line endings. A CRLF shebang fails on
  the Pi with "bad interpreter". `.gitattributes` handles this, but editors
  can still sneak CRLF in, and CI checks.
- Don't develop in the Pi's prod checkout (`/home/admin/prod`). Use
  `/home/admin/dev/novaGround` or your own clone. The exception is a lead's
  emergency fix during a test, which `Nova.ps1 hotfix` captures.
