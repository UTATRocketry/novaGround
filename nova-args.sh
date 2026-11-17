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
