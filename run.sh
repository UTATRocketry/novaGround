#!/usr/bin/env bash
# Interactive bench launcher: runs novaGround inside a tmux session you can
# attach to, detach from, and watch.
#
# This is NOT the production path. Production runs under systemd as
# novaGround@prod (see ./nova-pi.sh). Use this for bench work on the Pi.
#
# Settings come from the same files the service uses: /etc/nova/pi.env, and
# /etc/nova/novaGround-<env>.env for the environment you pick (dev unless you
# say otherwise), so a bench session uploads to the same backend the service
# would. Flags override them for one run.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Tests set NOVA_PI_ROOT to a scratch directory; on a Pi it is empty.
ROOT="${NOVA_PI_ROOT:-}"
ENVIRONMENT="dev"
TARGET=""; BROKER=""; BACKEND=""
KILL_SESSION="false"

print_help() {
  cat <<'EOF'
Usage: ./run.sh [options]

Options:
  --env <prod|dev>       Which environment's settings to use (default: dev)
  --target <name>        Binary to run: novaGround | novaThermo | novaMock
  --broker <host:port>   MQTT broker address (default: from the env file)
  --backend <host:port>  novaOps backend      (default: from the env file)
  --kill                 Kill the bench session and exit
  -h, --help             Show this help

Detach from the session with Ctrl+B then D. Reattach with:
  tmux attach -t NovaGround-<env>
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env)      ENVIRONMENT="$2"; shift 2 ;;
    --target)   TARGET="$2"; shift 2 ;;
    --broker)   BROKER="$2"; shift 2 ;;
    --backend)  BACKEND="$2"; shift 2 ;;
    --kill)     KILL_SESSION="true"; shift ;;
    -h|--help)  print_help; exit 0 ;;
    *)          echo "Unknown argument: $1"; print_help; exit 1 ;;
  esac
done

if [[ "$KILL_SESSION" == "true" ]]; then
  killed="false"
  # NovaGround is the session name older versions of this script used.
  for s in NovaGround-dev NovaGround-prod NovaGround; do
    tmux kill-session -t "$s" 2>/dev/null && { echo "Killed session $s"; killed="true"; }
  done
  [[ "$killed" == "true" ]] || echo "No bench session running"
  exit 0
fi

if [[ "$ENVIRONMENT" != "prod" && "$ENVIRONMENT" != "dev" ]]; then
  echo "--env must be prod or dev" >&2; exit 1
fi
SESSION="NovaGround-${ENVIRONMENT}"

ENV_FILE="${ROOT}/etc/nova/novaGround-${ENVIRONMENT}.env"
# shellcheck disable=SC1091
[[ -f "${ROOT}/etc/nova/pi.env" ]] && source "${ROOT}/etc/nova/pi.env"
# shellcheck disable=SC1090
[[ -f "$ENV_FILE" ]] && source "$ENV_FILE"

TARGET="${TARGET:-${NOVA_TARGET:-}}"
BROKER="${BROKER:-${NOVA_BROKER:-}}"
BACKEND="${BACKEND:-${NOVA_BACKEND:-}}"

if [[ -z "$TARGET" || -z "$BROKER" || -z "$BACKEND" ]]; then
  echo "Missing settings: target='${TARGET}' broker='${BROKER}' backend='${BACKEND}'." >&2
  echo "They come from /etc/nova/pi.env and ${ENV_FILE}." >&2
  echo "Run '.\\ops\\Nova.ps1 use ${ENVIRONMENT}' on the ops PC to write them, or pass --target/--broker/--backend." >&2
  exit 1
fi

BIN="${HERE}/build/${TARGET}"
if [[ ! -x "$BIN" ]]; then
  echo "Binary not found: $BIN" >&2
  echo "Build it first:  ./nova-pi.sh build --env ${ENVIRONMENT}" >&2
  exit 1
fi

# The service and a bench session can't share the hardware or the broker
# client ID; the symptom (telemetry flickering between two sources) is
# confusing to debug, so refuse instead of warning.
for e in prod dev; do
  if systemctl is-active --quiet "novaGround@${e}.service" || \
     { [[ "$e" == "prod" ]] && systemctl is-active --quiet novaGround.service; }; then
    echo "novaGround (${e}) is running as a service. Stop it before bench testing:" >&2
    echo "    sudo ./nova-pi.sh stop --env ${e}" >&2
    exit 1
  fi
done

CMD="NOVA_ENV=${ENVIRONMENT} NOVA_STATE_DIR=/var/lib/nova/${ENVIRONMENT} ${BIN} --broker ${BROKER} --backend ${BACKEND} ${NOVA_EXTRA_ARGS:-}"

if ! tmux has-session -t "$SESSION" 2>/dev/null; then
  tmux new-session -d -s "$SESSION"
  tmux rename-window -t "${SESSION}:0" 'Main'
  tmux send-keys -t "${SESSION}:0" "cd ${HERE}" C-m
  tmux send-keys -t "${SESSION}:0" "$CMD" C-m
  echo "Started session $SESSION: $CMD"
fi

tmux attach-session -t "${SESSION}:0"
