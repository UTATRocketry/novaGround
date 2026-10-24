#!/usr/bin/env bash
# Service entry point for novaGround. systemd runs this as novaGround@prod or
# novaGround@dev (see novaGround@.service).
#
# This is deliberately separate from run.sh: run.sh is the interactive tmux
# workflow for bench work, and ends in `tmux attach`, which can't work under
# systemd (no TTY). This script execs the binary directly in the foreground,
# which is what Type=simple expects.
#
# All settings come from the environment, which the unit loads from
# /etc/nova/pi.env and /etc/nova/novaGround-<env>.env. There are no default
# addresses here on purpose: a guessed backend is how prod data ended up on
# the dev port (issue #29).
#
# Exit code 78 means "don't retry": the unit's RestartPreventExitStatus=78
# stops systemd from looping on a problem a restart can't fix.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENVIRONMENT="${NOVA_ENV:-}"

fail() { echo "novaGround: $*" >&2; exit 78; }

[[ "$ENVIRONMENT" == "prod" || "$ENVIRONMENT" == "dev" ]] \
  || fail "NOVA_ENV is '${ENVIRONMENT}', expected prod or dev. Start it as novaGround@prod or novaGround@dev."
[[ -n "${NOVA_TARGET:-}" ]] \
  || fail "NOVA_TARGET is not set. Run: sudo ./nova-pi.sh install --name <Pi name> --target <binary>"
[[ -n "${NOVA_BROKER:-}" && -n "${NOVA_BACKEND:-}" ]] \
  || fail "NOVA_BROKER or NOVA_BACKEND is missing from /etc/nova/novaGround-${ENVIRONMENT}.env. Run '.\\ops\\Nova.ps1 use ${ENVIRONMENT}' on the ops PC, or 'sudo ./nova-pi.sh configure --env ${ENVIRONMENT} ...' here."

# The two environments share the hardware and the MQTT client ID. Running both
# makes telemetry flicker between two sources and fights over the relays.
OTHER="prod"; [[ "$ENVIRONMENT" == "prod" ]] && OTHER="dev"
if systemctl is-active --quiet "novaGround@${OTHER}.service"; then
  fail "novaGround@${OTHER} is running. Switch with: sudo ./nova-pi.sh use ${ENVIRONMENT}"
fi
if tmux has-session -t "NovaGround-${OTHER}" 2>/dev/null || tmux has-session -t "NovaGround-${ENVIRONMENT}" 2>/dev/null; then
  fail "a run.sh bench session is running. Stop it first: ./run.sh --kill"
fi

BIN="${HERE}/build/${NOVA_TARGET}"
if [[ ! -x "$BIN" ]]; then
  echo "novaGround: binary not found or not executable: $BIN" >&2
  echo "novaGround: build it with: ./nova-pi.sh build --env ${ENVIRONMENT}" >&2
  # A missing build can be fixed while the unit keeps retrying, so not 78.
  exit 1
fi

ARGS=(--broker "$NOVA_BROKER" --backend "$NOVA_BACKEND")

# Word-splitting is intended here so NOVA_EXTRA_ARGS can carry several flags.
# shellcheck disable=SC2206
[[ -n "${NOVA_EXTRA_ARGS:-}" ]] && ARGS+=($NOVA_EXTRA_ARGS)

echo "novaGround (${ENVIRONMENT}): exec $BIN ${ARGS[*]}"
exec "$BIN" "${ARGS[@]}"
