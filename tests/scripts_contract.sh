#!/usr/bin/env bash
# Contract: every flag the shell scripts can pass to the binary is a flag the
# binary's argument parser accepts (src/utils/cli.cpp). A mismatch once made a
# Pi restart forever (--fas-port).
set -euo pipefail

ROOT="${1:?usage: scripts_contract.sh <project source root>}"

# Flags the parser accepts: the usage text lists each one on its own line.
accepted="$(grep -ho '^ *<< "  --[a-z][a-z-]*' "$ROOT/src/utils/cli.cpp" | grep -o -- '--[a-z-]*' | sort -u)"
accepted="$accepted"$'\n'"--help"
[[ "$(wc -l <<<"$accepted")" -ge 6 ]] || { echo "could not read the accepted flags from cli.cpp" >&2; exit 2; }

# Flags the scripts put on the binary's command line, plus the example in
# the NOVA_EXTRA_ARGS comment that nova-pi.sh writes into pi.env.
passed="$(
  {
    grep -h '^ARGS=(' "$ROOT/start.sh" || true
    grep -h '^CMD=' "$ROOT/run.sh" || true
    grep -h 'Extra flags for this Pi' "$ROOT/nova-pi.sh" || true
  } | grep -o -- '--[a-z][a-z-]*' | sort -u
)"
[[ -n "$passed" ]] || { echo "found no binary flags in the scripts; the patterns need updating" >&2; exit 2; }

status=0
while read -r flag; do
  if ! grep -qx -- "$flag" <<<"$accepted"; then
    echo "scripts pass $flag but the binary does not accept it" >&2
    status=1
  fi
done <<<"$passed"
exit $status
