#!/usr/bin/env bats
# run.sh: the interactive tmux launcher for bench work.

load helpers

setup() {
  setup_sandbox
  CHECKOUT="$SANDBOX/checkout"
  mkdir -p "$CHECKOUT/build"
  cp "$REPO/run.sh" "$CHECKOUT/run.sh"
  printf '#!/usr/bin/env bash\n' > "$CHECKOUT/build/novaGround"
  chmod +x "$CHECKOUT/run.sh" "$CHECKOUT/build/novaGround"
  write_etc_nova pi.env NOVA_PI_NAME=NovaGround NOVA_TARGET=novaGround
  write_etc_nova novaGround-dev.env NOVA_BROKER=pc:1884 NOVA_BACKEND=pc:8001
  write_etc_nova novaGround-prod.env NOVA_BROKER=pc:1883 NOVA_BACKEND=pc:8000
  unset NOVA_EXTRA_ARGS NOVA_TARGET NOVA_BROKER NOVA_BACKEND
}

teardown() { teardown_sandbox; }

@test "defaults to the dev environment and sends the command to tmux" {
  run "$CHECKOUT/run.sh"
  [ "$status" -eq 0 ]
  grep -q 'tmux new-session -d -s NovaGround-dev' "$CALLS"
  grep -q 'NOVA_ENV=dev NOVA_STATE_DIR=/var/lib/nova/dev .*/build/novaGround --broker pc:1884 --backend pc:8001' "$CALLS"
  grep -q 'tmux attach-session -t NovaGround-dev:0' "$CALLS"
}

@test "--env prod uses prod's settings and session name" {
  run "$CHECKOUT/run.sh" --env prod
  [ "$status" -eq 0 ]
  grep -q 'new-session -d -s NovaGround-prod' "$CALLS"
  grep -q -- '--broker pc:1883 --backend pc:8000' "$CALLS"
}

@test "flags override the env file for one run" {
  run "$CHECKOUT/run.sh" --broker other:1 --backend other:2 --target novaGround
  [ "$status" -eq 0 ]
  grep -q -- '--broker other:1 --backend other:2' "$CALLS"
}

@test "rejects an unknown environment and an unknown argument" {
  run "$CHECKOUT/run.sh" --env staging
  [ "$status" -eq 1 ]
  [[ "$output" == *"--env must be prod or dev"* ]]
  run "$CHECKOUT/run.sh" --bogus
  [ "$status" -eq 1 ]
  [[ "$output" == *"Unknown argument"* ]]
}

@test "refuses when settings are missing" {
  rm "$NOVA_PI_ROOT/etc/nova/novaGround-dev.env"
  run "$CHECKOUT/run.sh"
  [ "$status" -eq 1 ]
  [[ "$output" == *"Missing settings"* ]]
  ! grep -q 'new-session' "$CALLS"
}

@test "refuses when the binary is not built" {
  rm "$CHECKOUT/build/novaGround"
  run "$CHECKOUT/run.sh"
  [ "$status" -eq 1 ]
  [[ "$output" == *"Binary not found"* ]]
}

@test "refuses while either service instance is running" {
  fake_active novaGround@prod.service
  run "$CHECKOUT/run.sh"
  [ "$status" -eq 1 ]
  [[ "$output" == *"running as a service"* ]]
  ! grep -q 'new-session' "$CALLS"
}

@test "refuses while the old single service is running" {
  fake_active novaGround.service
  run "$CHECKOUT/run.sh" --env prod
  [ "$status" -eq 1 ]
}

@test "does not start a second tmux session when one exists" {
  fake_session NovaGround-dev
  run "$CHECKOUT/run.sh"
  [ "$status" -eq 0 ]
  ! grep -q 'new-session' "$CALLS"
  grep -q 'attach-session' "$CALLS"
}

@test "--kill kills known sessions and says so when there are none" {
  run "$CHECKOUT/run.sh" --kill
  [ "$status" -eq 0 ]
  [[ "$output" == *"No bench session running"* ]]
  fake_session NovaGround-dev
  run "$CHECKOUT/run.sh" --kill
  [ "$status" -eq 0 ]
  [[ "$output" == *"Killed session NovaGround-dev"* ]]
}
