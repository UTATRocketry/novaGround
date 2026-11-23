# shellcheck shell=bash
# Sourced by start.sh and run.sh (not run on its own): turns the NOVA_*
# settings in the environment into the binary's command line, so the service
# and a bench session pass exactly the same flags.
#
# build_args fills ARGS. A setting that is unset or empty leaves its flag off
# and the binary uses its own default. Values are passed on as written: the
# binary rejects a malformed one (and reports a bad stats setting without
# stopping), so nothing here decides what is allowed.
#
# Order matters, because the binary takes the last occurrence of a flag:
#   1. flags from the settings below
#   2. NOVA_GLOBAL_ARGS  from /etc/nova/pi.env, for every environment on this Pi
#   3. NOVA_EXTRA_ARGS   from /etc/nova/novaGround-<env>.env, for one environment
# so a per-environment value overrides a global one, and either overrides the
# individual settings.
#
# Bench-safety flags (--stop-actuate) are allowed only where the Pi's env file
# sets NOVA_BENCH_FLAGS on. The binary accepts them everywhere (it never decides
# whether a setting is allowed); check_bench_flags is the policy, and it looks at
# the finished command line, so a flag arriving by any route is covered:
# NOVA_GLOBAL_ARGS, NOVA_EXTRA_ARGS or, in run.sh, the arguments after `--`.
# With the setting off (the default in prod) a flag left in an env file by mistake
# stops the instance from starting, rather than stopping it from driving valves.
#
# NOVA_SIM_SCENARIO and NOVA_STATE_DIR are not flags: the binary reads them
# from its environment. start.sh and run.sh export them.

# build_args <broker> <backend>
build_args() {
  ARGS=(--broker "$1" --backend "$2")
  [[ -n "${NOVA_VERBOSITY:-}" ]] && ARGS+=(--verbosity "$NOVA_VERBOSITY")
  [[ -n "${NOVA_SAMPLE_MS:-}" ]] && ARGS+=(--sample-ms "$NOVA_SAMPLE_MS")
  [[ -n "${NOVA_PUBLISH_MS:-}" ]] && ARGS+=(--publish-ms "$NOVA_PUBLISH_MS")
  [[ -n "${NOVA_STATS:-}" ]] && ARGS+=(--stats "$NOVA_STATS")
  [[ -n "${NOVA_STATS_MS:-}" ]] && ARGS+=(--stats-ms "$NOVA_STATS_MS")
  # Word-splitting is intended here so these can carry several flags.
  # shellcheck disable=SC2206
  [[ -n "${NOVA_GLOBAL_ARGS:-}" ]] && ARGS+=($NOVA_GLOBAL_ARGS)
  # shellcheck disable=SC2206
  [[ -n "${NOVA_EXTRA_ARGS:-}" ]] && ARGS+=($NOVA_EXTRA_ARGS)
  return 0
}

# The flags check_bench_flags polices.
BENCH_FLAGS=(--stop-actuate)

# bench_flags_allowed: NOVA_BENCH_FLAGS is on (on, 1, true or yes, any case).
bench_flags_allowed() {
  case "${NOVA_BENCH_FLAGS:-}" in
    [Oo][Nn]|1|[Tt][Rr][Uu][Ee]|[Yy][Ee][Ss]) return 0 ;;
    *) return 1 ;;
  esac
}

# check_bench_flags <environment>: call after build_args. Prints why and returns 1
# when ARGS holds a bench-safety flag that NOVA_BENCH_FLAGS does not allow.
check_bench_flags() {
  bench_flags_allowed && return 0
  local arg flag
  for arg in "${ARGS[@]}"; do
    for flag in "${BENCH_FLAGS[@]}"; do
      if [[ "$arg" == "$flag" ]]; then
        echo "novaGround: $flag is not allowed in the $1 environment: NOVA_BENCH_FLAGS is off." >&2
        echo "novaGround: to allow it on this Pi:" >&2
        echo "novaGround:     sudo ./nova-pi.sh configure --env $1 --bench-flags on" >&2
        return 1
      fi
    done
  done
  return 0
}
