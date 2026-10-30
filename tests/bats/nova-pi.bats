#!/usr/bin/env bats
# nova-pi.sh with NOVA_PI_ROOT pointing at a scratch directory and the
# service manager, tmux and sudo replaced by stubs (see helpers.bash).

load helpers

setup() {
  setup_sandbox
  NP="$REPO/nova-pi.sh"
}

teardown() { teardown_sandbox; }

# ── argument handling ────────────────────────────────────────────────────────

@test "no action prints the usage" {
  run "$NP"
  [ "$status" -eq 0 ]
  [[ "$output" == *"Usage:"* ]]
}

@test "unknown action and unknown argument are errors" {
  run "$NP" frobnicate
  [ "$status" -eq 1 ]
  run "$NP" status --bogus
  [ "$status" -eq 1 ]
}

@test "--env must be prod or dev" {
  run "$NP" start --env staging
  [ "$status" -eq 1 ]
  [[ "$output" == *"must be prod or dev"* ]]
}

@test "a flag without a value is an error" {
  run "$NP" configure --env
  [ "$status" -eq 1 ]
  [[ "$output" == *"needs a value"* ]]
}

# ── configure ────────────────────────────────────────────────────────────────

@test "configure writes the env file for that environment only" {
  run "$NP" configure --env dev --broker pc:1884 --backend pc:8001 --console http://pc:8090
  [ "$status" -eq 0 ]
  f="$NOVA_PI_ROOT/etc/nova/novaGround-dev.env"
  grep -qx 'NOVA_BROKER=pc:1884' "$f"
  grep -qx 'NOVA_BACKEND=pc:8001' "$f"
  grep -qx 'NOVA_CONSOLE=http://pc:8090' "$f"
  [ ! -e "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env" ]
}

@test "configure needs env, broker and backend" {
  run "$NP" configure --env dev --broker pc:1884
  [ "$status" -eq 1 ]
  [ ! -e "$NOVA_PI_ROOT/etc/nova/novaGround-dev.env" ]
}

@test "configure keeps the existing console URL when none is given" {
  make_installed_pi
  run "$NP" configure --env prod --broker pc:1999 --backend pc:8999
  [ "$status" -eq 0 ]
  grep -qx 'NOVA_CONSOLE=http://pc:8090' "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env"
  grep -qx 'NOVA_BROKER=pc:1999' "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env"
}

@test "configure restarts the environment only if it is running" {
  make_installed_pi
  run "$NP" configure --env dev --broker a:1 --backend b:2
  ! called "systemctl restart novaGround@dev.service"
  fake_active novaGround@dev.service
  run "$NP" configure --env dev --broker a:1 --backend b:2
  called "systemctl restart novaGround@dev.service"
}

# ── use ──────────────────────────────────────────────────────────────────────

@test "use dev stops prod and starts dev" {
  make_installed_pi
  fake_active novaGround@prod.service
  run "$NP" use dev
  [ "$status" -eq 0 ]
  called "systemctl stop novaGround@prod.service"
  called "systemctl start novaGround@dev.service"
  [ ! -f "$FAKE_STATE/units/novaGround@prod.service" ]
  [ -f "$FAKE_STATE/units/novaGround@dev.service" ]
}

@test "use never leaves both environments running" {
  make_installed_pi
  fake_active novaGround@dev.service
  run "$NP" use prod
  [ "$status" -eq 0 ]
  [ -f "$FAKE_STATE/units/novaGround@prod.service" ]
  [ ! -f "$FAKE_STATE/units/novaGround@dev.service" ]
}

@test "use refuses an unknown environment and a missing argument" {
  make_installed_pi
  run "$NP" use staging
  [ "$status" -eq 1 ]
  run "$NP" use
  [ "$status" -eq 1 ]
}

@test "use exits 3 on a Pi that still has the old single unit" {
  write_etc_nova novaGround-dev.env NOVA_BROKER=a NOVA_BACKEND=b
  run "$NP" use dev
  [ "$status" -eq 3 ]
  [[ "$output" == *"install"* ]]
}

@test "use refuses when the environment has no settings, and stops nothing" {
  make_installed_pi
  rm "$NOVA_PI_ROOT/etc/nova/novaGround-dev.env"
  fake_active novaGround@prod.service
  run "$NP" use dev
  [ "$status" -eq 1 ]
  [ -f "$FAKE_STATE/units/novaGround@prod.service" ]
  ! called "systemctl stop novaGround@prod.service"
}

@test "use refuses while a bench session is running, and stops nothing" {
  make_installed_pi
  fake_session NovaGround-dev
  fake_active novaGround@prod.service
  run "$NP" use dev
  [ "$status" -eq 1 ]
  [[ "$output" == *"bench session"* ]]
  [ -f "$FAKE_STATE/units/novaGround@prod.service" ]
}

@test "use fails loudly when the new instance does not stay up" {
  make_installed_pi
  FAKE_START_FAILS=1 run "$NP" use dev
  [ "$status" -eq 1 ]
  [[ "$output" == *"did not stay up"* ]]
  called "journalctl -u novaGround@dev.service -n 15 --no-pager"
}

# ── start / stop / restart ───────────────────────────────────────────────────

@test "start refuses when the other environment is running" {
  make_installed_pi
  fake_active novaGround@prod.service
  run "$NP" start --env dev
  [ "$status" -eq 1 ]
  ! called "systemctl start novaGround@dev.service"
}

@test "start defaults to prod when nothing is running" {
  make_installed_pi
  run "$NP" start
  [ "$status" -eq 0 ]
  called "systemctl start novaGround@prod.service"
}

@test "stop stops whichever environment is running" {
  make_installed_pi
  fake_active novaGround@dev.service
  run "$NP" stop
  [ "$status" -eq 0 ]
  called "systemctl stop novaGround@dev.service"
}

@test "stop with nothing running says so and stops nothing" {
  run "$NP" stop
  [ "$status" -eq 0 ]
  [[ "$output" == *"Nothing is running"* ]]
  ! grep -q 'systemctl stop' "$CALLS"
}

@test "restart restarts the running environment" {
  make_installed_pi
  fake_active novaGround@dev.service
  run "$NP" restart
  [ "$status" -eq 0 ]
  called "systemctl restart novaGround@dev.service"
}

# ── install / uninstall ──────────────────────────────────────────────────────

@test "install writes pi.env, the units and the tool copy, and enables prod only" {
  run "$NP" install --name NovaThermo --target novaThermo
  [ "$status" -eq 0 ]
  grep -qx 'NOVA_PI_NAME=NovaThermo' "$NOVA_PI_ROOT/etc/nova/pi.env"
  grep -qx 'NOVA_TARGET=novaThermo' "$NOVA_PI_ROOT/etc/nova/pi.env"
  [ -f "$NOVA_PI_ROOT/etc/systemd/system/novaGround@.service" ]
  [ -f "$NOVA_PI_ROOT/etc/systemd/system/nova-pi-report.timer" ]
  [ -x "$NOVA_PI_ROOT/usr/local/lib/nova/nova-pi.sh" ]
  called "systemctl enable novaGround@prod.service"
  called "systemctl disable novaGround@dev.service"
  ! called "systemctl enable novaGround@dev.service"
}

@test "install rejects an unknown target" {
  run "$NP" install --target novaBogus
  [ "$status" -eq 1 ]
  [ ! -e "$NOVA_PI_ROOT/etc/nova/pi.env" ]
}

@test "install does not rewrite settings it already has" {
  make_installed_pi
  before="$(cat "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env")"
  run "$NP" install
  [ "$status" -eq 0 ]
  [ "$(cat "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env")" = "$before" ]
  grep -qx 'NOVA_TARGET=novaGround' "$NOVA_PI_ROOT/etc/nova/pi.env"
}

@test "install migrates the old single unit and env file, then restarts prod" {
  mkdir -p "$NOVA_PI_ROOT/etc/systemd/system"
  echo "[Unit]" > "$NOVA_PI_ROOT/etc/systemd/system/novaGround.service"
  write_etc_nova novaGround.env NOVA_BROKER=old:1883 NOVA_BACKEND=old:8000
  fake_active novaGround.service
  run "$NP" install --target novaGround
  [ "$status" -eq 0 ]
  [ ! -e "$NOVA_PI_ROOT/etc/systemd/system/novaGround.service" ]
  [ ! -e "$NOVA_PI_ROOT/etc/nova/novaGround.env" ]
  [ -f "$NOVA_PI_ROOT/etc/nova/novaGround.env.old" ]
  grep -qx 'NOVA_BROKER=old:1883' "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env"
  grep -qx 'NOVA_BACKEND=old:8000' "$NOVA_PI_ROOT/etc/nova/novaGround-prod.env"
  called "systemctl disable --now novaGround.service"
  called "systemctl start novaGround@prod.service"
}

@test "uninstall removes the units and tool copy but leaves settings" {
  run "$NP" install --target novaGround
  [ "$status" -eq 0 ]
  run "$NP" uninstall
  [ "$status" -eq 0 ]
  [ ! -e "$NOVA_PI_ROOT/etc/systemd/system/novaGround@.service" ]
  [ ! -e "$NOVA_PI_ROOT/usr/local/lib/nova/nova-pi.sh" ]
  [ -f "$NOVA_PI_ROOT/etc/nova/pi.env" ]
}

@test "install refuses to run from the installed tool copy" {
  run "$NP" install --target novaGround
  [ "$status" -eq 0 ]
  run "$NOVA_PI_ROOT/usr/local/lib/nova/nova-pi.sh" install
  [ "$status" -eq 1 ]
  [[ "$output" == *"checkout"* ]]
}

# ── status ───────────────────────────────────────────────────────────────────

@test "status --json is valid JSON and reports the running environment" {
  python3 -c 'import pwd' 2>/dev/null || skip "needs a POSIX python3"
  make_installed_pi
  fake_active novaGround@dev.service
  run "$NP" status --json
  [ "$status" -eq 0 ]
  python3 - "$output" <<'PY'
import json, sys
snap = json.loads(sys.argv[1])
assert snap["name"] == "NovaGround", snap
assert snap["target"] == "novaGround", snap
assert snap["instances"]["dev"]["state"] == "active", snap
assert snap["instances"]["prod"]["state"] != "active", snap
PY
}

# ── safety: tests never escape the sandbox ───────────────────────────────────

@test "no action in this file ever calls sudo" {
  make_installed_pi
  fake_active novaGround@prod.service
  "$NP" use dev >/dev/null 2>&1
  "$NP" restart >/dev/null 2>&1
  "$NP" stop >/dev/null 2>&1
  "$NP" install >/dev/null 2>&1
  assert_no_sudo
}
