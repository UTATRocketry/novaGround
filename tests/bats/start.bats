#!/usr/bin/env bats
# start.sh: the systemd entry point. Exit 78 means "don't retry".

load helpers

setup() {
  setup_sandbox
  # start.sh finds build/<target> next to itself, so run a copy.
  CHECKOUT="$SANDBOX/checkout"
  mkdir -p "$CHECKOUT/build"
  cp "$REPO/start.sh" "$CHECKOUT/start.sh"
  printf '#!/usr/bin/env bash\necho "BINARY ran with: $*"\n' > "$CHECKOUT/build/novaGround"
  chmod +x "$CHECKOUT/start.sh" "$CHECKOUT/build/novaGround"
  export NOVA_ENV=dev NOVA_TARGET=novaGround NOVA_BROKER=pc:1884 NOVA_BACKEND=pc:8001
  unset NOVA_EXTRA_ARGS
}

teardown() { teardown_sandbox; }

@test "execs the binary with the broker and backend" {
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"BINARY ran with: --broker pc:1884 --backend pc:8001"* ]]
}

@test "NOVA_EXTRA_ARGS is split into separate flags" {
  NOVA_EXTRA_ARGS="--verbosity 2 --publish-ms 100" run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--backend pc:8001 --verbosity 2 --publish-ms 100"* ]]
}

@test "exit 78 when NOVA_ENV is missing or not prod/dev" {
  NOVA_ENV= run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
  NOVA_ENV=staging run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
}

@test "exit 78 when NOVA_TARGET is missing" {
  NOVA_TARGET= run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
  [[ "$output" == *"NOVA_TARGET"* ]]
}

@test "exit 78 when the broker or backend is missing" {
  NOVA_BROKER= run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
  NOVA_BACKEND= run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
}

@test "exit 78 when the other environment's service is running" {
  fake_active novaGround@prod.service
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
  [[ "$output" == *"novaGround@prod is running"* ]]
}

@test "the same environment's service running does not block it" {
  fake_active novaGround@dev.service
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
}

@test "exit 78 when a run.sh bench session exists, for either environment" {
  fake_session NovaGround-prod
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
  rm "$FAKE_STATE/tmux/NovaGround-prod"
  fake_session NovaGround-dev
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 78 ]
}

@test "a missing build is retryable: exit 1, not 78" {
  rm "$CHECKOUT/build/novaGround"
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 1 ]
  [[ "$output" == *"build it with"* ]]
}

@test "a binary that is not executable is retryable too" {
  [[ "$(uname -s)" == Linux ]] || skip "needs POSIX file modes"
  chmod -x "$CHECKOUT/build/novaGround"
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 1 ]
}
