#!/usr/bin/env bash
# End to end on the real application with simulated hardware: novaMock against a
# private broker. Checks that telemetry carries the scenario's values and that a
# data-saving command records them to CSV.
#
# Sandbox (TESTING-PLAN.md section 2.1): the broker listens on loopback on a
# free port and is never 1883; everything novaMock writes goes under a scratch
# directory; the "backend" is a closed port, so nothing is uploaded anywhere.
set -euo pipefail

NOVAMOCK="${1:?usage: novamock_smoke.sh <path to novaMock>}"
WORK="$(mktemp -d)"
PIDS=()

cleanup() {
  for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done
  wait 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; [[ -f "$WORK/novamock.log" ]] && sed 's/^/  novaMock: /' "$WORK/novamock.log" >&2; exit 1; }

PORT="$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
case "$PORT" in
  1883|8000|8001|3000|3001|8080|8081) echo "refusing protected port $PORT" >&2; exit 2 ;;
esac

printf 'listener %s 127.0.0.1\nallow_anonymous true\n' "$PORT" > "$WORK/mosquitto.conf"
mosquitto -c "$WORK/mosquitto.conf" > "$WORK/mosquitto.log" 2>&1 &
PIDS+=($!)
for _ in $(seq 50); do
  if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then break; fi
  sleep 0.1
done

# One MCC 128 with a flat 2.5 V on channel 3, and GPIO input 5 held high.
cat > "$WORK/scenario.json" <<'EOF'
{"hats":[{"address":0,"type":"mcc128","channels":{"3":{"wave":"flat","value":2.5}}}],
 "gpio_inputs":{"5":1}}
EOF

mkdir "$WORK/run"
cd "$WORK/run"   # novaMock records to ./data
NOVA_ENV=dev \
NOVA_STATE_DIR="$WORK/state" \
NOVA_SIM_SCENARIO="$WORK/scenario.json" \
  "$NOVAMOCK" --broker "127.0.0.1:$PORT" --backend "http://127.0.0.1:9" \
              --sample-ms 20 --publish-ms 50 > "$WORK/novamock.log" 2>&1 &
PIDS+=($!)

# 1. Telemetry with the scenario's sensor value and GPIO input arrives.
cat > "$WORK/check_telemetry.py" <<'EOF'
import json, sys
for line in sys.stdin:
    try:
        msg = json.loads(line)
    except ValueError:
        continue
    sensors = {(s["hat_id"], s["channel_id"]): s["value"] for s in msg.get("sensors", [])}
    gpios = {g["pin_id"]: g["state"] for g in msg.get("gpios", [])}
    if sensors.get((0, 3)) == 2.5 and gpios.get(5) == 1:
        sys.exit(0)
sys.exit(1)
EOF
python3 "$WORK/check_telemetry.py" < <(mosquitto_sub -h 127.0.0.1 -p "$PORT" -t nova/telemetry/engine -W 20) \
  || fail "no telemetry with hat 0 channel 3 = 2.5 and GPIO 5 = 1"
echo "ok: telemetry carries the scenario values"

# 2. A data-saving command reaches the logger, which records the sampled rows.
mosquitto_pub -h 127.0.0.1 -p "$PORT" -q 1 -t nova/command \
  -m '{"source":"novaOps","command":{"type":"data_file","action":"start_data_saving","filename":"smoke"}}'
sleep 1
mosquitto_pub -h 127.0.0.1 -p "$PORT" -q 1 -t nova/command \
  -m '{"source":"novaOps","command":{"type":"data_file","action":"stop_data_saving"}}'

CSV="$WORK/run/data/novaMock_smoke_sensors.csv"
for _ in $(seq 50); do [[ -f "$WORK/state/last_upload.json" ]] && break; sleep 0.1; done
[[ -f "$CSV" ]] || fail "no sensor CSV at $CSV"
[[ "$(head -n 1 "$CSV")" == "timestamp,hat0_ch0,hat0_ch1,hat0_ch2,hat0_ch3,hat0_ch4,hat0_ch5,hat0_ch6,hat0_ch7" ]] \
  || fail "unexpected CSV header: $(head -n 1 "$CSV")"
[[ "$(wc -l < "$CSV")" -ge 3 ]] || fail "expected at least two data rows in $CSV"
ROW="$(sed -n 2p "$CSV")"
[[ "$(cut -d, -f5 <<<"$ROW")" == "2.5" ]] || fail "hat0_ch3 should be 2.5 in row: $ROW"
echo "ok: the command recorded the sampled rows"

# 3. The upload to the unreachable backend was attempted and reported as failed.
[[ -f "$WORK/state/last_upload.json" ]] || fail "no last_upload.json was written"
python3 -c 'import json,sys; d = json.load(open(sys.argv[1])); sys.exit(0 if d.get("ok") is False else 1)' \
  "$WORK/state/last_upload.json" || fail "last_upload.json should report a failed upload: $(cat "$WORK/state/last_upload.json")"
echo "ok: the failed upload was reported"
