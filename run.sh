#!/usr/bin/env bash
# Interactive bench launcher: runs novaGround inside a tmux session you can
# attach to, detach from, and watch.
#
# This is NOT the production path — production runs under systemd via
# start.sh (see ./nova-pi.sh install). Use this for dev work on the Pi.
#
# Configuration is read from /etc/nova/novaGround.env if present, so the bench
# session and the service agree on broker/backend addresses, and can be
# overridden per-invocation with flags or environment variables.
set -uo pipefail

SESSION="NovaGround"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Load shared config if it exists, then let flags win over it.
# shellcheck disable=SC1091
[[ -f /etc/nova/novaGround.env ]] && source /etc/nova/novaGround.env

TARGET="${NOVA_TARGET:-novaGround}"
BROKER="${NOVA_BROKER:-192.168.137.1}"
BACKEND="${NOVA_BACKEND:-192.168.137.1:8000}"
FAS_PORT="${NOVA_FAS_PORT:-}"

print_help() {
  cat <<'EOF'
Usage: ./run.sh [options]

Options:
  --target <name>     Binary to run: novaGround | novaThermo | novaMock
  --broker <host>     MQTT broker address     (default: 192.168.137.1)
  --backend <host:port>  novaOps backend base (default: 192.168.137.1:8000)
  --fas-port <dev>    RS-422 serial device, e.g. /dev/ttyUSB0 (default: none)
  --kill              Kill the tmux session and exit
  -h, --help          Show this help

Detach from the session with Ctrl+B then D. Reattach with:
  tmux attach -t NovaGround
EOF
}

KILL_SESSION="false"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)   TARGET="$2"; shift 2 ;;
    --broker)   BROKER="$2"; shift 2 ;;
    --backend)  BACKEND="$2"; shift 2 ;;
    --fas-port) FAS_PORT="$2"; shift 2 ;;
    --kill)     KILL_SESSION="true"; shift ;;
    -h|--help)  print_help; exit 0 ;;
    *)          echo "Unknown argument: $1"; print_help; exit 1 ;;
  esac
done

if [[ "$KILL_SESSION" == "true" ]]; then
  tmux kill-session -t "$SESSION" 2>/dev/null && echo "Killed session $SESSION" \
    || echo "No session $SESSION"
  exit 0
fi

BIN="${HERE}/build/${TARGET}"
if [[ ! -x "$BIN" ]]; then
  echo "Binary not found: $BIN" >&2
  echo "Build it first:  meson compile -C build" >&2
  exit 1
fi

# Warn rather than fail: the prod service and this session cannot share the
# serial port or the broker client id, and the resulting symptom (telemetry
# that flickers between two sources) is confusing to debug.
if systemctl is-active --quiet novaGround.service; then
  echo "WARNING: the novaGround service is running. Stop it before bench testing:" >&2
  echo "         sudo systemctl stop novaGround" >&2
fi

CMD="${BIN} --broker ${BROKER} --backend ${BACKEND}"
if [[ -n "$FAS_PORT" ]]; then
  CMD="${CMD} --fas-port ${FAS_PORT}"
fi

if ! tmux has-session -t "$SESSION" 2>/dev/null; then
  tmux new-session -d -s "$SESSION"
  tmux rename-window -t "${SESSION}:0" 'Main'
  tmux send-keys -t "${SESSION}:0" "cd ${HERE}" C-m
  tmux send-keys -t "${SESSION}:0" "$CMD" C-m
  echo "Started session $SESSION: $CMD"
fi

tmux attach-session -t "${SESSION}:0"
