#!/usr/bin/env bats
# start.sh: the systemd entry point. Exit 78 means "don't retry".

load helpers

setup() {
  setup_sandbox
  # start.sh finds build/<target> next to itself, so run a copy.
  CHECKOUT="$SANDBOX/checkout"
  mkdir -p "$CHECKOUT/build"
  cp "$REPO/start.sh" "$REPO/nova-args.sh" "$CHECKOUT/"
  printf '#!/usr/bin/env bash\necho "BINARY ran with: $*"\n' > "$CHECKOUT/build/novaGround"
  chmod +x "$CHECKOUT/start.sh" "$CHECKOUT/build/novaGround"
  export NOVA_ENV=dev NOVA_TARGET=novaGround NOVA_BROKER=pc:1884 NOVA_BACKEND=pc:8001
  unset NOVA_EXTRA_ARGS NOVA_GLOBAL_ARGS NOVA_VERBOSITY NOVA_SAMPLE_MS NOVA_PUBLISH_MS NOVA_STATS NOVA_STATS_MS NOVA_SIM_SCENARIO NOVA_STATE_DIR
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

@test "NOVA_STATS and NOVA_STATS_MS become flags, as written" {
  NOVA_STATS=1 NOVA_STATS_MS=250 run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--backend pc:8001 --stats 1 --stats-ms 250"* ]]
}

@test "a stats value is passed through even if it is wrong; the binary reports it" {
  NOVA_STATS=maybe run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--stats maybe"* ]]
}

@test "no stats flags when the stats settings are unset or empty" {
  NOVA_STATS= run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" != *"--stats"* ]]
}

@test "NOVA_EXTRA_ARGS comes after the stats flags, so it can override them" {
  NOVA_STATS=1 NOVA_EXTRA_ARGS="--stats off" run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--stats 1 --stats off"* ]]
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

@test "NOVA_VERBOSITY, NOVA_SAMPLE_MS and NOVA_PUBLISH_MS become flags" {
  NOVA_VERBOSITY=2 NOVA_SAMPLE_MS=20 NOVA_PUBLISH_MS=100 run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--backend pc:8001 --verbosity 2 --sample-ms 20 --publish-ms 100"* ]]
}

@test "empty NOVA_VERBOSITY, NOVA_SAMPLE_MS and NOVA_PUBLISH_MS leave the flags off" {
  NOVA_VERBOSITY= NOVA_SAMPLE_MS= NOVA_PUBLISH_MS= run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"BINARY ran with: --broker pc:1884 --backend pc:8001"* ]]
  [[ "$output" != *"--verbosity"* && "$output" != *"--sample-ms"* && "$output" != *"--publish-ms"* ]]
}

@test "NOVA_GLOBAL_ARGS comes before NOVA_EXTRA_ARGS, so the environment wins" {
  NOVA_GLOBAL_ARGS="--verbosity 1" NOVA_EXTRA_ARGS="--verbosity 2" run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--verbosity 1 --verbosity 2"* ]]
}

@test "NOVA_GLOBAL_ARGS comes after the individual settings" {
  NOVA_PUBLISH_MS=50 NOVA_GLOBAL_ARGS="--publish-ms 200" run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"--publish-ms 50 --publish-ms 200"* ]]
}

@test "the binary gets NOVA_STATE_DIR and NOVA_SIM_SCENARIO in its environment" {
  printf '#!/usr/bin/env bash\necho "STATE=${NOVA_STATE_DIR:-} SCENARIO=${NOVA_SIM_SCENARIO:-}"\n' > "$CHECKOUT/build/novaGround"
  NOVA_STATE_DIR=/srv/nova NOVA_SIM_SCENARIO=/srv/run.json run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"STATE=/srv/nova SCENARIO=/srv/run.json"* ]]
}

@test "NOVA_STATE_DIR defaults to this environment's folder; no scenario unless set" {
  printf '#!/usr/bin/env bash\necho "STATE=${NOVA_STATE_DIR:-} SCENARIO=${NOVA_SIM_SCENARIO:-}"\n' > "$CHECKOUT/build/novaGround"
  run "$CHECKOUT/start.sh"
  [ "$status" -eq 0 ]
  [[ "$output" == *"STATE=/var/lib/nova/dev SCENARIO="* ]]
}
