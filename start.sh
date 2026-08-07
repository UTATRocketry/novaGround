#!/usr/bin/env bash
# Service entry point for novaGround. Invoked by systemd (see novaGround.service).
#
# This is deliberately separate from run.sh: run.sh is the interactive tmux
# workflow for bench work, and ends in `tmux attach`, which cannot work under
# systemd (no TTY). This script execs the binary directly in the foreground,
# which is what Type=simple expects.
#
# All configuration comes from the environment, set by the unit file and
# /etc/nova/novaGround.env.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET="${NOVA_TARGET:-novaGround}"
BROKER="${NOVA_BROKER:-192.168.137.1}"
BACKEND="${NOVA_BACKEND:-192.168.137.1:8000}"
FAS_PORT="${NOVA_FAS_PORT:-}"
EXTRA_ARGS="${NOVA_EXTRA_ARGS:-}"

BIN="${HERE}/build/${TARGET}"
if [[ ! -x "$BIN" ]]; then
  echo "novaGround: binary not found or not executable: $BIN" >&2
  echo "novaGround: build it with 'meson compile -C build' (or ./nova-pi.sh build)" >&2
  exit 1
fi

ARGS=(--broker "$BROKER" --backend "$BACKEND")

# The FAS serial link is optional. Only pass --fas-port when the device is
# actually present, otherwise the binary fails at startup on a Pi that has no
# RS-422 adapter plugged in and systemd restarts it forever.
if [[ -n "$FAS_PORT" ]]; then
  if [[ -e "$FAS_PORT" ]]; then
    ARGS+=(--fas-port "$FAS_PORT")
    [[ -n "${NOVA_FAS_BAUD:-}" ]] && ARGS+=(--fas-baud "$NOVA_FAS_BAUD")
  else
    echo "novaGround: NOVA_FAS_PORT=$FAS_PORT is set but the device is absent; starting without the FAS link" >&2
  fi
fi

# Word-splitting is intended here so NOVA_EXTRA_ARGS can carry several flags.
# shellcheck disable=SC2206
[[ -n "$EXTRA_ARGS" ]] && ARGS+=($EXTRA_ARGS)

echo "novaGround: exec $BIN ${ARGS[*]}"
exec "$BIN" "${ARGS[@]}"
