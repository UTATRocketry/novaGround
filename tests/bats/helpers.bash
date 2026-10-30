# Shared setup for the bats suites.
#
# Every test runs in a scratch directory (NOVA_PI_ROOT) with stubs for
# systemctl, tmux, sudo, journalctl and sleep first on PATH. The stubs log
# their arguments to $CALLS and keep their fake state under $SANDBOX, so a
# test can never start a service or touch /etc on the machine running it.

# read_var in nova-pi.sh uses grep -P, which needs a UTF-8 locale.
export LC_ALL=C.UTF-8

REPO="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"

setup_sandbox() {
  SANDBOX="$(mktemp -d)"
  export NOVA_PI_ROOT="$SANDBOX/root"
  export CALLS="$SANDBOX/calls.log"
  export FAKE_STATE="$SANDBOX/state"      # active units and tmux sessions
  STUBS="$SANDBOX/stubs"
  mkdir -p "$NOVA_PI_ROOT/etc/systemd/system" "$FAKE_STATE/units" "$FAKE_STATE/tmux" "$STUBS"
  : > "$CALLS"

  # systemctl: is-active/start/stop/restart keep state; everything else just logs.
  cat > "$STUBS/systemctl" <<'EOF'
#!/usr/bin/env bash
echo "systemctl $*" >> "$CALLS"
args=(); quiet=""
for a in "$@"; do case "$a" in --quiet) quiet=1 ;; --*) ;; *) args+=("$a") ;; esac; done
verb="${args[0]:-}"; unit="${args[1]:-}"
case "$verb" in
  is-active)
    if [[ -f "$FAKE_STATE/units/$unit" ]]; then [[ -n "$quiet" ]] || echo active; exit 0; fi
    [[ -n "$quiet" ]] || echo inactive
    exit 3 ;;
  is-enabled) echo disabled; exit 1 ;;
  start|restart)
    [[ -n "${FAKE_START_FAILS:-}" ]] && exit 0   # accepted, but never comes up
    touch "$FAKE_STATE/units/$unit" ;;
  stop) rm -f "$FAKE_STATE/units/$unit" ;;
  disable) [[ " $* " == *" --now "* ]] && rm -f "$FAKE_STATE/units/$unit" ;;
esac
exit 0
EOF

  cat > "$STUBS/tmux" <<'EOF'
#!/usr/bin/env bash
echo "tmux $*" >> "$CALLS"
case "$1" in
  has-session) [[ -f "$FAKE_STATE/tmux/$3" ]] ;;
  list-sessions) ls "$FAKE_STATE/tmux" 2>/dev/null; exit 0 ;;
  kill-session) [[ -f "$FAKE_STATE/tmux/$3" ]] && rm -f "$FAKE_STATE/tmux/$3" ;;
  *) exit 0 ;;
esac
EOF

  cat > "$STUBS/sudo" <<'EOF'
#!/usr/bin/env bash
echo "sudo $*" >> "$CALLS"
echo "sudo must not be called in tests" >&2
exit 99
EOF

  printf '#!/usr/bin/env bash\necho "journalctl $*" >> "$CALLS"\necho "fake journal line"\n' > "$STUBS/journalctl"
  printf '#!/usr/bin/env bash\nexit 0\n' > "$STUBS/sleep"
  chmod +x "$STUBS"/*
  export PATH="$STUBS:$PATH"
}

teardown_sandbox() {
  [[ -n "${SANDBOX:-}" && -d "$SANDBOX" ]] && rm -rf "$SANDBOX"
}

fake_active() { touch "$FAKE_STATE/units/$1"; }
fake_session() { touch "$FAKE_STATE/tmux/$1"; }

# write_env_file <name> <KEY=VALUE>...  (under the sandboxed /etc/nova)
write_etc_nova() {
  local name="$1"; shift
  mkdir -p "$NOVA_PI_ROOT/etc/nova"
  printf '%s\n' "$@" > "$NOVA_PI_ROOT/etc/nova/$name"
}

# A configured Pi: template unit, pi.env and both environments' settings.
make_installed_pi() {
  mkdir -p "$NOVA_PI_ROOT/etc/systemd/system"
  echo "[Unit]" > "$NOVA_PI_ROOT/etc/systemd/system/novaGround@.service"
  write_etc_nova pi.env NOVA_PI_NAME=NovaGround NOVA_TARGET=novaGround
  write_etc_nova novaGround-prod.env NOVA_BROKER=pc:1883 NOVA_BACKEND=pc:8000 NOVA_CONSOLE=http://pc:8090
  write_etc_nova novaGround-dev.env NOVA_BROKER=pc:1884 NOVA_BACKEND=pc:8001 NOVA_CONSOLE=http://pc:8090
}

# No test may reach a real service manager or write outside the sandbox.
assert_no_sudo() { ! grep -q '^sudo ' "$CALLS"; }
called() { grep -qxF "$1" "$CALLS"; }
