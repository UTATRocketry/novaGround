#!/usr/bin/env bash
# ============================================================================
#  nova-pi.sh - install, switch, build, deploy and inspect novaGround on a
#  Raspberry Pi.
#
#  Mirrors ops/Nova.ps1 on the Windows ops PC so operators learn one set of
#  verbs for the whole ground station. Design: docs/design/pi-orchestration.md
#  in Nova-Collected.
#
#  Usage:
#      sudo ./nova-pi.sh install [--name NovaGround|NovaThermo] [--target novaGround|novaThermo|novaMock]
#      sudo ./nova-pi.sh use prod|dev
#      sudo ./nova-pi.sh configure --env prod|dev [--broker <host[:port]>] [--backend <host[:port]>]
#           [--console <host[:port]|url>] [--verbosity <0|1|2>] [--sample-ms <ms>] [--publish-ms <ms>]
#           [--stats <on|off>] [--stats-ms <ms>] [--bench-flags <on|off>] [--sim-scenario <file>] [--state-dir <dir>]
#           [--extra-args "<flags>"] [--unset NOVA_<NAME>] [--global-args "<flags>"]
#           ./nova-pi.sh build [--env prod|dev]
#      sudo ./nova-pi.sh start | stop | restart [--env prod|dev]
#           ./nova-pi.sh status [--json] | metrics --json | logs [--env prod|dev] [-f] | doctor | report
#      sudo ./nova-pi.sh update [--env prod|dev]
#      sudo ./nova-pi.sh deploy [<tag>|<commit>] [--release <label>]
#      sudo ./nova-pi.sh uninstall
#
#  There is one systemd instance per environment, like the PC's two stacks:
#  novaGround@prod (enabled, starts on boot) and novaGround@dev (started only
#  by `use dev`). Only one runs at a time.
#
#  configure changes only the settings you pass and keeps the rest of that
#  environment's file. --unset removes one (--unset NOVA_STATS). --broker,
#  --backend and --console take host, host:port or a full URL; a missing port
#  gets the default. --global-args is the one setting that is not per
#  environment: it goes in /etc/nova/pi.env and applies to both. An
#  environment's --extra-args come after it, so they win.
#
#  You rarely need `use` or `configure` here: Nova.ps1 on the ops PC writes
#  /etc/nova/novaGround-<env>.env and switches every Pi when it switches the
#  PC. They're for when the PC can't reach the Pi.
#
#  deploy puts a pinned Nova release into prod. With a tag it asks the Nova
#  Console which novaGround commit that release pins; with no argument it uses
#  the newest stable release. The commit comes from GitHub or, offline, from
#  the dev checkout it was tested in.
# ============================================================================
set -uo pipefail

# Tests set NOVA_PI_ROOT to a scratch directory so every path below lands in it
# and nothing on the real Pi is touched. On a Pi it is empty.
ROOT="${NOVA_PI_ROOT:-}"

PROD_DIR="${ROOT}/home/admin/prod/novaGround"
DEV_DIR="${ROOT}/home/admin/dev/novaGround"
UNIT_DIR="${ROOT}/etc/systemd/system"
TEMPLATE_UNIT="${UNIT_DIR}/novaGround@.service"
REPORT_SERVICE="nova-pi-report.service"
REPORT_TIMER="nova-pi-report.timer"
# The single unit and env file used before novaGround@prod / @dev.
LEGACY_UNIT="${UNIT_DIR}/novaGround.service"
ENV_DIR="${ROOT}/etc/nova"
LEGACY_ENV="${ENV_DIR}/novaGround.env"
# Which Pi this is and which binary it runs. Owned by this Pi.
PI_ENV="${ENV_DIR}/pi.env"
# What prod is running, written by `deploy`.
DEPLOYED_FILE="${ENV_DIR}/deployed"
STATE_ROOT="${ROOT}/var/lib/nova"
# A copy of this script and its Python helper that outlive any checkout, run by
# the report timer.
TOOL_DIR="${ROOT}/usr/local/lib/nova"
TOOL="${TOOL_DIR}/nova-pi.sh"
REPORT_VERSION=1
TARGETS="novaGround novaThermo novaMock"
# The Pi's hardware watchdog (see setup_hw_watchdog). Bookworm keeps config.txt
# in /boot/firmware; older releases in /boot.
BOOT_CONFIGS="${ROOT}/boot/firmware/config.txt ${ROOT}/boot/config.txt"
SYSTEMD_CONF="${ROOT}/etc/systemd/system.conf"
RUNTIME_WATCHDOG_SEC=10

HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
# Status, the report and the release lookup. It sits beside this script, in the
# checkout and in TOOL_DIR.
PI_TOOLS="${HERE}/nova-pi-tools.py"

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; RESET=$'\033[0m'
step() { echo "${CYAN}==> $*${RESET}"; }
ok()   { echo "${GREEN}    $*${RESET}"; }
warn() { echo "${YELLOW}    $*${RESET}"; }
err()  { echo "${RED}    $*${RESET}" >&2; }

usage() { sed -n '3,/^# =\{10\}/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

need_root() {
  # A test root can't harm the Pi: its paths are scratch and its commands are stubs.
  if [[ -n "$ROOT" ]]; then return; fi
  if [[ $EUID -ne 0 ]]; then
    err "This action needs root. Re-run with sudo:  sudo $0 $ACTION"
    exit 1
  fi
}

# ── small helpers ────────────────────────────────────────────────────────────

repo_dir()  { if [[ "$1" == "dev" ]]; then echo "$DEV_DIR"; else echo "$PROD_DIR"; fi; }
other_env() { if [[ "$1" == "prod" ]]; then echo "dev"; else echo "prod"; fi; }
unit()      { echo "novaGround@$1.service"; }
env_file()  { echo "${ENV_DIR}/novaGround-$1.env"; }
owner_of()  { stat -c %U "$1"; }

# Run as the checkout's owner when we're root, so git and meson never leave
# root-owned files that break the next ordinary `git pull` or build.
as_owner() {
  local dir="$1"; shift
  if [[ $EUID -eq 0 && -d "$dir" && "$(owner_of "$dir")" != "root" ]]; then
    sudo -u "$(owner_of "$dir")" "$@"
  else
    "$@"
  fi
}

# read_var <file> <KEY>: a value from a KEY=VALUE file, or nothing.
read_var() {
  [[ -f "$1" ]] || return 0
  grep -oP "^$2=\K.*" "$1" | tail -n 1 | sed -e 's/^"//' -e 's/"$//'
}

active_env() {
  local e
  for e in prod dev; do
    if systemctl is-active --quiet "$(unit "$e")"; then echo "$e"; return; fi
  done
}

check_env() {
  if [[ "$1" != "prod" && "$1" != "dev" ]]; then err "The environment must be prod or dev, not '$1'."; exit 1; fi
}

# The environment to act on: --env if given, else whichever is running, else prod.
pick_env() {
  if [[ -n "$ENVIRONMENT" ]]; then echo "$ENVIRONMENT"; return; fi
  local a; a="$(active_env)"
  echo "${a:-prod}"
}

bench_session() {
  local s
  for s in NovaGround-dev NovaGround-prod NovaGround; do
    if as_owner "$PROD_DIR" tmux has-session -t "$s" 2>/dev/null; then echo "$s"; return; fi
  done
}

http_ok() {
  python3 -c 'import sys, urllib.request; urllib.request.urlopen(sys.argv[1], timeout=3)' "$1" >/dev/null 2>&1
}

# NOVA_CONSOLE as a base URL: host and host:port work like NOVA_BROKER and
# NOVA_BACKEND (nova-pi-tools.py has the rules). Prints nothing when it's empty.
console_url() { [[ -n "${1:-}" ]] && python3 "$PI_TOOLS" console-url "$1"; return 0; }

report_now() { systemctl start --no-block "$REPORT_SERVICE" >/dev/null 2>&1 || true; }

# git writes scripts with the mode it stores. Releases before
# bugfix/script-exec-bit stored them without the executable bit, so put it
# back after anything that checks files out.
make_executable() { chmod +x "$1"/*.sh 2>/dev/null || true; }

assert_checkout() {
  if [[ "$HERE" == "$TOOL_DIR" ]]; then
    err "Run $ACTION from a novaGround checkout (e.g. $DEV_DIR/nova-pi.sh), not from $TOOL."
    exit 1
  fi
}

# ── argument parsing ─────────────────────────────────────────────────────────

ACTION="${1:-}"
shift || true
ENVIRONMENT=""; POSITIONAL=""; TARGET=""; PI_NAME=""
RELEASE_LABEL=""
FOLLOW="false"; JSON="false"
# What `configure` was asked to set (setting name -> value) and to remove, and
# the new NOVA_GLOBAL_ARGS if one was given. They are not applied while parsing
# so that a missing or bad value stops before anything is written.
declare -A SET=()
UNSET=()
GLOBAL_ARGS=""; GLOBAL_ARGS_GIVEN="false"

need_value() { if [[ $# -lt 2 || -z "$2" ]]; then err "$1 needs a value"; exit 1; fi; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env)       need_value "$@"; ENVIRONMENT="$2"; shift 2 ;;
    --target)    need_value "$@"; TARGET="$2"; shift 2 ;;
    --name)      need_value "$@"; PI_NAME="$2"; shift 2 ;;
    --broker)       need_value "$@"; SET[NOVA_BROKER]="$2"; shift 2 ;;
    --backend)      need_value "$@"; SET[NOVA_BACKEND]="$2"; shift 2 ;;
    --console)      need_value "$@"; SET[NOVA_CONSOLE]="$2"; shift 2 ;;
    --verbosity)    need_value "$@"; SET[NOVA_VERBOSITY]="$2"; shift 2 ;;
    --sample-ms)    need_value "$@"; SET[NOVA_SAMPLE_MS]="$2"; shift 2 ;;
    --publish-ms)   need_value "$@"; SET[NOVA_PUBLISH_MS]="$2"; shift 2 ;;
    --stats)        need_value "$@"; SET[NOVA_STATS]="$2"; shift 2 ;;
    --stats-ms)     need_value "$@"; SET[NOVA_STATS_MS]="$2"; shift 2 ;;
    --bench-flags)  need_value "$@"; SET[NOVA_BENCH_FLAGS]="$2"; shift 2 ;;
    --sim-scenario) need_value "$@"; SET[NOVA_SIM_SCENARIO]="$2"; shift 2 ;;
    --state-dir)    need_value "$@"; SET[NOVA_STATE_DIR]="$2"; shift 2 ;;
    --extra-args)   need_value "$@"; SET[NOVA_EXTRA_ARGS]="$2"; shift 2 ;;
    --global-args)  need_value "$@"; GLOBAL_ARGS="$2"; GLOBAL_ARGS_GIVEN="true"; shift 2 ;;
    --unset)        need_value "$@"; UNSET+=("$2"); shift 2 ;;
    --release)   need_value "$@"; RELEASE_LABEL="$2"; shift 2 ;;
    -f|--follow) FOLLOW="true"; shift ;;
    --json)      JSON="true"; shift ;;
    -h|--help)   usage; exit 0 ;;
    -*) err "Unknown argument: $1"; usage; exit 1 ;;
    *)
      if [[ -z "$POSITIONAL" && ( "$ACTION" == "deploy" || "$ACTION" == "use" ) ]]; then
        POSITIONAL="$1"; shift
      else
        err "Unknown argument: $1"; usage; exit 1
      fi ;;
  esac
done

[[ -z "$ENVIRONMENT" ]] || check_env "$ENVIRONMENT"

# A value goes into a KEY=VALUE file that systemd and bash both read, so it
# can't hold a double quote or a newline.
BAD_CHARS=$'"\n'
for k in "${!SET[@]}"; do
  if [[ "${SET[$k]}" == *["$BAD_CHARS"]* ]]; then err "The value for $k can't contain a double quote or a newline."; exit 1; fi
done
if [[ "$GLOBAL_ARGS" == *["$BAD_CHARS"]* ]]; then err "--global-args can't contain a double quote or a newline."; exit 1; fi

# ── settings files ───────────────────────────────────────────────────────────

# The settings an environment's file can hold, in the order they're written.
# start.sh and run.sh turn them into flags (nova-args.sh), except
# NOVA_SIM_SCENARIO and NOVA_STATE_DIR, which the binary reads itself, and
# NOVA_BENCH_FLAGS, which the scripts read to decide whether a flag is allowed.
ENV_KEYS=(NOVA_BROKER NOVA_BACKEND NOVA_CONSOLE NOVA_VERBOSITY NOVA_SAMPLE_MS NOVA_PUBLISH_MS
          NOVA_STATS NOVA_STATS_MS NOVA_BENCH_FLAGS NOVA_SIM_SCENARIO NOVA_STATE_DIR NOVA_EXTRA_ARGS)
declare -A ENV_HELP=(
  [NOVA_BROKER]="MQTT broker on the ops PC: host or host:port (default port 1883)."
  [NOVA_BACKEND]="novaOps backend: host or host:port (default port 8000)."
  [NOVA_CONSOLE]="Nova Console: host, host:port (default port 8080) or a URL. Used by status, report and deploy."
  [NOVA_VERBOSITY]="Log detail: 0 quiet, 1 info, 2 debug."
  [NOVA_SAMPLE_MS]="DAQ sampling interval in ms."
  [NOVA_PUBLISH_MS]="Telemetry publish interval in ms."
  [NOVA_STATS]="Runtime instrumentation: on or off."
  [NOVA_STATS_MS]="How often stats are published, in ms."
  [NOVA_BENCH_FLAGS]="Allow bench-safety flags such as --stop-actuate (on or off). start.sh and run.sh refuse them when off."
  [NOVA_SIM_SCENARIO]="Scenario file for the simulated hardware (novaMock)."
  [NOVA_STATE_DIR]="Where novaGround keeps last_upload.json (default /var/lib/nova/<env>)."
  [NOVA_EXTRA_ARGS]="More flags for this environment only; they come after NOVA_GLOBAL_ARGS in pi.env."
)

# Setting values by name: the file being read or written.
declare -A CFG=()

# fmt_value <value>: as it is written in a file. Quoted when it has spaces, which
# systemd and bash both accept and read_var strips again.
fmt_value() { if [[ "$1" == *[[:space:]]* ]]; then printf '"%s"' "$1"; else printf '%s' "$1"; fi; }

# load_env_file <env>: CFG <- that environment's file, every key (empty if absent).
load_env_file() {
  local k f; f="$(env_file "$1")"
  CFG=()
  for k in "${ENV_KEYS[@]}"; do CFG[$k]="$(read_var "$f" "$k")"; done
}

# write_env_file <env> <where the values came from>: the file <- CFG. A key with
# no value is written commented out, so the file lists every setting.
write_env_file() {
  local f k; f="$(env_file "$1")"
  mkdir -p "$ENV_DIR"
  {
    echo "# novaGround settings for the $1 environment ($2)."
    echo "# Nova.ps1 on the ops PC rewrites this file on every 'use', 'deploy' and"
    echo "# 'get-changes', from ops/nova.settings.ps1: change the settings there."
    for k in "${ENV_KEYS[@]}"; do
      echo "# ${ENV_HELP[$k]}"
      if [[ -n "${CFG[$k]:-}" ]]; then echo "$k=$(fmt_value "${CFG[$k]}")"; else echo "#$k="; fi
    done
  } > "$f"
  chmod 644 "$f"
}

# write_pi_env <name> <target> <global args>
write_pi_env() {
  mkdir -p "$ENV_DIR"
  {
    echo "# Which Pi this is and which binary it runs. Written by nova-pi.sh install;"
    echo "# change it with:  sudo ./nova-pi.sh install --name <name> --target <binary>"
    echo "# NOVA_PI_NAME must match this Pi's name in ops/nova.settings.ps1 on the ops PC."
    echo "NOVA_PI_NAME=$1"
    echo "NOVA_TARGET=$2"
    echo
    echo "# Flags for this Pi's binary in every environment, e.g. \"--verbosity 2\"."
    echo "# An environment's own NOVA_EXTRA_ARGS (in novaGround-<env>.env) come after"
    echo "# these, so they win. Change it with:  sudo ./nova-pi.sh configure --env <env> --global-args \"...\""
    if [[ -n "$3" ]]; then echo "NOVA_GLOBAL_ARGS=$(fmt_value "$3")"; else echo "#NOVA_GLOBAL_ARGS="; fi
  } > "$PI_ENV"
  chmod 644 "$PI_ENV"
}

# /etc/nova/pi.env: --name/--target win, then the existing file, then the old
# single env file. Asks for the target only when there's nothing to go on.
ensure_pi_env() {
  local target name global
  target="${TARGET:-$(read_var "$PI_ENV" NOVA_TARGET)}"
  target="${target:-$(read_var "$LEGACY_ENV" NOVA_TARGET)}"
  if [[ -z "$target" ]]; then
    if [[ -t 0 ]]; then read -rp "    Binary this Pi runs (novaGround/novaThermo/novaMock) [novaGround]: " target; fi
    target="${target:-novaGround}"
  fi
  if [[ " $TARGETS " != *" $target "* ]]; then err "Unknown target '$target' (one of: $TARGETS)"; exit 1; fi

  name="${PI_NAME:-$(read_var "$PI_ENV" NOVA_PI_NAME)}"
  # novaThermo -> NovaThermo: the name this Pi has in ops/nova.settings.ps1.
  name="${name:-${target^}}"

  # Flags used to be NOVA_EXTRA_ARGS here, for every environment. That name now
  # belongs to the per-environment files, so the old value moves to NOVA_GLOBAL_ARGS.
  global="$(read_var "$PI_ENV" NOVA_GLOBAL_ARGS)"
  if [[ -z "$global" ]]; then
    if [[ -f "$PI_ENV" ]]; then global="$(read_var "$PI_ENV" NOVA_EXTRA_ARGS)"
    else global="$(read_var "$LEGACY_ENV" NOVA_EXTRA_ARGS)"; fi
  fi

  if [[ -f "$PI_ENV" && "$(read_var "$PI_ENV" NOVA_TARGET)" == "$target" && \
        "$(read_var "$PI_ENV" NOVA_PI_NAME)" == "$name" ]] && ! grep -q '^NOVA_EXTRA_ARGS=' "$PI_ENV"; then
    return
  fi
  write_pi_env "$name" "$target" "$global"
  ok "Wrote $PI_ENV (${name}, runs ${target})"
}

# Replace the old single novaGround.service. Sets LEGACY_WAS_ACTIVE.
LEGACY_WAS_ACTIVE="false"
migrate_legacy() {
  if [[ -f "$LEGACY_UNIT" ]]; then
    step "Replacing the old novaGround.service with novaGround@prod / novaGround@dev"
    systemctl is-active --quiet novaGround.service && LEGACY_WAS_ACTIVE="true"
    systemctl disable --now novaGround.service >/dev/null 2>&1
    rm -f "$LEGACY_UNIT"
    ok "Removed $LEGACY_UNIT"
  fi
  if [[ -f "$LEGACY_ENV" ]]; then
    # The old file was always prod's (the service only ever ran prod).
    if [[ ! -f "$(env_file prod)" ]]; then
      CFG=()
      CFG[NOVA_BROKER]="$(read_var "$LEGACY_ENV" NOVA_BROKER)"
      CFG[NOVA_BACKEND]="$(read_var "$LEGACY_ENV" NOVA_BACKEND)"
      write_env_file prod "carried over from $LEGACY_ENV"
      ok "Carried the old broker and backend over into $(env_file prod)"
    fi
    mv "$LEGACY_ENV" "${LEGACY_ENV}.old"
    ok "Kept the old settings as ${LEGACY_ENV}.old"
  fi
}

# Units, the report timer and the tool copy, from the checkout this runs from.
setup_units() {
  assert_checkout
  for f in "novaGround@.service" "$REPORT_SERVICE" "$REPORT_TIMER" "nova-pi-tools.py"; do
    [[ -f "${HERE}/$f" ]] || { err "${HERE} has no $f. Update this checkout first."; exit 1; }
  done
  mkdir -p "$ENV_DIR" "$STATE_ROOT" "$TOOL_DIR"
  ensure_pi_env
  migrate_legacy

  install -m 644 "${HERE}/novaGround@.service" "$TEMPLATE_UNIT"
  install -m 644 "${HERE}/${REPORT_SERVICE}" "${UNIT_DIR}/${REPORT_SERVICE}"
  install -m 644 "${HERE}/${REPORT_TIMER}" "${UNIT_DIR}/${REPORT_TIMER}"
  install -m 755 "${HERE}/nova-pi.sh" "$TOOL"
  install -m 755 "$PI_TOOLS" "${TOOL_DIR}/nova-pi-tools.py"
  make_executable "$PROD_DIR"
  make_executable "$DEV_DIR"

  systemctl daemon-reload
  # Prod comes back on boot; dev never does, exactly like the PC's stacks.
  systemctl enable "$(unit prod)" >/dev/null 2>&1
  systemctl disable "$(unit dev)" >/dev/null 2>&1 || true
  systemctl enable --now "$REPORT_TIMER" >/dev/null 2>&1
  ok "Installed novaGround@.service, the report timer and $TOOL (with nova-pi-tools.py)"
}

# The Pi's config.txt, or nothing if this isn't a Pi.
boot_config() {
  local f
  for f in $BOOT_CONFIGS; do [[ -f "$f" ]] && { echo "$f"; return 0; }; done
  return 1
}

# Sets KEY=VALUE in a file: replaces the first line that sets KEY (commented
# out or not) and removes any later ones, or appends the line (after the
# optional HEADER line, e.g. "[all]" so a config.txt filter section above
# doesn't apply to it). Changes nothing, and returns 1, when the line is already
# there and nothing else sets KEY.
set_conf_line() {
  local file="$1" key="$2" line="$3" header="${4:-}"
  local pattern="^[#[:space:]]*${key}[[:space:]]*="
  if grep -qxF -- "$line" "$file" && [[ "$(grep -cE -- "$pattern" "$file")" -eq 1 ]]; then
    return 1  # already set
  fi
  if grep -qE -- "$pattern" "$file"; then
    # First match becomes the line; any other match is dropped.
    awk -v pat="$pattern" -v line="$line" '
      $0 ~ pat { if (!done) { print line; done = 1 } ; next } { print }' "$file" > "${file}.nova-tmp" \
      && cat "${file}.nova-tmp" > "$file" && rm -f "${file}.nova-tmp"
  else
    # A file that doesn't end in a newline would glue the line onto its last one.
    [[ -s "$file" && -n "$(tail -c 1 "$file")" ]] && echo >> "$file"
    [[ -n "$header" ]] && echo "$header" >> "$file"
    echo "$line" >> "$file"
  fi
  return 0
}

# The Pi's hardware watchdog (docs/design/pi-io-config.md, "Watchdogs"):
# dtparam=watchdog=on in config.txt turns the chip on, and RuntimeWatchdogSec in
# systemd's system.conf makes systemd ping it, so a hung kernel or systemd resets
# the Pi. It applies to the whole Pi, not one environment, so it's set here and
# not in the env files. Both take effect after a reboot. Idempotent.
setup_hw_watchdog() {
  local cfg changed=""
  if cfg="$(boot_config)"; then
    set_conf_line "$cfg" "dtparam=watchdog" "dtparam=watchdog=on" "[all]" && changed=1
    ok "Hardware watchdog on in $cfg (dtparam=watchdog=on)"
  else
    warn "No config.txt in /boot/firmware or /boot: not a Pi? Hardware watchdog not set."
  fi
  if [[ -f "$SYSTEMD_CONF" ]]; then
    set_conf_line "$SYSTEMD_CONF" "RuntimeWatchdogSec" "RuntimeWatchdogSec=${RUNTIME_WATCHDOG_SEC}" && changed=1
    ok "systemd pings it every ${RUNTIME_WATCHDOG_SEC}s or less ($SYSTEMD_CONF: RuntimeWatchdogSec=${RUNTIME_WATCHDOG_SEC})"
  else
    warn "No $SYSTEMD_CONF: RuntimeWatchdogSec not set."
  fi
  [[ -n "$changed" ]] && warn "The hardware watchdog settings changed: they take effect after a reboot (sudo reboot)."
  return 0
}

# ── actions ──────────────────────────────────────────────────────────────────

do_build() {
  local env="${1:-${ENVIRONMENT:-prod}}"
  local dir; dir="$(repo_dir "$env")"
  [[ -d "$dir" ]] || { err "Missing repo: $dir"; exit 1; }

  step "Building novaGround ($env) in $dir"
  # Older deploys built as root; hand the build folder back to the owner.
  if [[ $EUID -eq 0 && -d "${dir}/build" ]]; then chown -R "$(owner_of "$dir"):" "${dir}/build"; fi
  # Without root we can't fix that, and the compiler only says
  # "Operation not permitted" deep inside ninja. Say what to do instead.
  if [[ $EUID -ne 0 && -d "${dir}/build" ]] && \
     [[ -n "$(find "${dir}/build" ! -user "$(id -un)" -print -quit 2>/dev/null)" ]]; then
    err "${dir}/build has files owned by another user (an older build ran as root)."
    err "Build with sudo once; it hands build/ back to $(owner_of "$dir"):  sudo $0 build --env $env"
    exit 1
  fi
  if [[ ! -d "${dir}/build" ]]; then
    step "Setting up the meson build directory"
    as_owner "$dir" bash -c 'cd "$1" && CC=clang CXX=clang++ meson setup build' _ "$dir" \
      || { err "meson setup failed"; exit 1; }
  fi
  as_owner "$dir" meson compile -C "${dir}/build" || { err "build failed"; exit 1; }
  ok "Build complete."
  local f
  for f in "${dir}/build/"nova*; do
    [[ -e "$f" ]] && echo "    ${f##*/}"
  done
  return 0
}

do_install() {
  need_root
  step "Installing novaGround from $HERE"
  setup_units
  setup_hw_watchdog

  local e f target
  target="$(read_var "$PI_ENV" NOVA_TARGET)"
  for e in prod dev; do
    f="$(env_file "$e")"
    if [[ -f "$f" ]]; then ok "$e settings: backend $(read_var "$f" NOVA_BACKEND) ($f)"
    else warn "$e settings: none yet. Nova.ps1 writes $f on '.\\ops\\Nova.ps1 use $e'."; fi
    if [[ ! -x "$(repo_dir "$e")/build/${target}" ]]; then warn "$e has no ${target} build yet: $0 build --env $e"; fi
  done

  if [[ "$LEGACY_WAS_ACTIVE" == "true" ]]; then
    if [[ -f "$(env_file prod)" ]]; then
      systemctl start "$(unit prod)" && ok "Restarted as novaGround@prod (the old service was running)."
    else
      warn "The old service was running, but there are no prod settings to start novaGround@prod with."
    fi
  fi
  report_now
  echo
  ok "novaGround@prod starts on boot. Switch stacks from the ops PC with '.\\ops\\Nova.ps1 use prod|dev',"
  ok "or here with:  sudo $0 use prod|dev"
}

do_configure() {
  need_root
  local usage="sudo $0 configure --env prod|dev [--broker <host[:port]>] [--backend <host[:port]>] [--console <host[:port]|url>] [--stats <on|off>] ... (see: $0 --help)"
  if [[ -z "$ENVIRONMENT" ]]; then
    err "Usage: $usage"
    exit 1
  fi
  if [[ ${#SET[@]} -eq 0 && ${#UNSET[@]} -eq 0 && "$GLOBAL_ARGS_GIVEN" != "true" ]]; then
    err "Nothing to change. Usage: $usage"
    exit 1
  fi

  local k
  load_env_file "$ENVIRONMENT"
  for k in "${UNSET[@]}"; do
    [[ " ${ENV_KEYS[*]} " == *" $k "* ]] || { err "--unset takes one of: ${ENV_KEYS[*]}"; exit 1; }
    CFG[$k]=""
  done
  for k in "${!SET[@]}"; do CFG[$k]="${SET[$k]}"; done

  # Broker and backend are the two settings start.sh refuses to run without.
  if [[ -z "${CFG[NOVA_BROKER]}" || -z "${CFG[NOVA_BACKEND]}" ]]; then
    err "$(env_file "$ENVIRONMENT") needs both a broker and a backend: add --broker <host[:port]> --backend <host[:port]>."
    err "Normally Nova.ps1 writes this for you ('.\\ops\\Nova.ps1 use $ENVIRONMENT' on the ops PC)."
    exit 1
  fi
  if [[ "$GLOBAL_ARGS_GIVEN" == "true" && ! -f "$PI_ENV" ]]; then
    err "No $PI_ENV yet. Run: sudo $0 install"
    exit 1
  fi

  if [[ ${#SET[@]} -gt 0 || ${#UNSET[@]} -gt 0 ]]; then
    write_env_file "$ENVIRONMENT" "written by hand with nova-pi.sh configure"
    ok "Wrote $(env_file "$ENVIRONMENT"): backend ${CFG[NOVA_BACKEND]}"
  fi
  if [[ "$GLOBAL_ARGS_GIVEN" == "true" ]]; then
    write_pi_env "$(read_var "$PI_ENV" NOVA_PI_NAME)" "$(read_var "$PI_ENV" NOVA_TARGET)" "$GLOBAL_ARGS"
    ok "Wrote NOVA_GLOBAL_ARGS to $PI_ENV (applies to prod and dev)"
  fi

  # NOVA_GLOBAL_ARGS reaches both environments, so restart whichever is running.
  local e
  for e in prod dev; do
    if [[ "$e" == "$ENVIRONMENT" || "$GLOBAL_ARGS_GIVEN" == "true" ]] && systemctl is-active --quiet "$(unit "$e")"; then
      systemctl restart "$(unit "$e")" && ok "Restarted novaGround@$e to pick it up."
    fi
  done
  report_now
}

do_use() {
  need_root
  local env="${POSITIONAL:-$ENVIRONMENT}"
  [[ -n "$env" ]] || { err "Usage: sudo $0 use prod|dev"; exit 1; }
  check_env "$env"
  local other; other="$(other_env "$env")"

  if [[ ! -f "$TEMPLATE_UNIT" ]]; then
    err "This Pi still has the old single novaGround.service. Migrate it first:"
    err "    sudo ${DEV_DIR}/nova-pi.sh install"
    exit 3
  fi
  if [[ ! -f "$(env_file "$env")" ]]; then
    err "No $env settings ($(env_file "$env")). Run '.\\ops\\Nova.ps1 use $env' on the ops PC,"
    err "or write them here: sudo $0 configure --env $env --broker <host:port> --backend <host:port>"
    exit 1
  fi
  local s; s="$(bench_session)"
  if [[ -n "$s" ]]; then
    err "A run.sh bench session ($s) is running and would fight the service. Stop it first: ./run.sh --kill"
    exit 1
  fi

  step "Switching this Pi to $env"
  if systemctl is-active --quiet "$(unit "$other")"; then
    systemctl stop "$(unit "$other")" && ok "stopped novaGround@$other"
  fi
  systemctl start "$(unit "$env")" || true
  # start.sh refuses some problems at once; give it a moment to show.
  sleep 2
  if ! systemctl is-active --quiet "$(unit "$env")"; then
    err "novaGround@$env did not stay up:"
    journalctl -u "$(unit "$env")" -n 15 --no-pager | sed 's/^/      /' >&2
    report_now
    exit 1
  fi
  ok "novaGround@$env is running (backend $(read_var "$(env_file "$env")" NOVA_BACKEND))"
  report_now
}

do_start() {
  need_root
  local env; env="$(pick_env)"
  local other; other="$(other_env "$env")"
  if systemctl is-active --quiet "$(unit "$other")"; then
    err "novaGround@$other is running. Switch instead: sudo $0 use $env"; exit 1
  fi
  step "Starting novaGround@$env"
  systemctl start "$(unit "$env")" && ok "started"
  report_now
}

do_stop() {
  need_root
  local env="${ENVIRONMENT:-$(active_env)}"
  if [[ -z "$env" ]]; then ok "Nothing is running."; return; fi
  step "Stopping novaGround@$env"
  systemctl stop "$(unit "$env")" && ok "stopped"
  report_now
}

do_restart() {
  need_root
  local env; env="$(pick_env)"
  step "Restarting novaGround@$env"
  systemctl restart "$(unit "$env")" && ok "restarted"
  report_now
}

# One snapshot of this Pi, as JSON (--json), for people (status) or POSTed to
# the Nova Console (report). The console's /api/pi-report reads this format;
# bump REPORT_VERSION if it changes incompatibly.
snapshot() {
  PROD_DIR="$PROD_DIR" DEV_DIR="$DEV_DIR" ENV_DIR="$ENV_DIR" PI_ENV="$PI_ENV"   STATE_ROOT="$STATE_ROOT" DEPLOYED_FILE="$DEPLOYED_FILE" LEGACY_UNIT="$LEGACY_UNIT"   REPORT_VERSION="$REPORT_VERSION" BOOT_CONFIGS="$BOOT_CONFIGS" SYSTEMD_CONF="$SYSTEMD_CONF" RUNTIME_WATCHDOG_SEC="$RUNTIME_WATCHDOG_SEC" python3 "$PI_TOOLS" snapshot "$1"
}

do_status() {
  if [[ "$JSON" == "true" ]]; then snapshot json; return; fi
  snapshot human
  local env; env="$(active_env)"
  if [[ -n "$env" ]]; then
    echo "  recent log (novaGround@$env)"
    journalctl -u "$(unit "$env")" -n 8 --no-pager 2>/dev/null | sed 's/^/    /'
    echo
  fi
}

do_report() { snapshot report; }

# Read-only system metrics (CPU, memory, temperature, throttling, the running
# instance, disk I/O, clock sync) as JSON. Needs no sudo and writes nothing.
do_metrics() {
  [[ "$JSON" == "true" ]] || { err "Usage: $0 metrics --json"; exit 1; }
  python3 "$PI_TOOLS" metrics
}

do_logs() {
  local env; env="$(pick_env)"
  if [[ "$FOLLOW" == "true" ]]; then
    journalctl -u "$(unit "$env")" -f
  else
    journalctl -u "$(unit "$env")" -n 100 --no-pager
  fi
}

do_update() {
  need_root
  local env="${ENVIRONMENT:-dev}"
  local dir; dir="$(repo_dir "$env")"
  [[ -d "$dir" ]] || { err "Missing repo: $dir"; exit 1; }
  if [[ "$env" == "prod" ]]; then
    warn "Prod normally changes only through 'Nova.ps1 deploy'. Updating it by hand anyway."
  fi

  step "Updating novaGround ($env) in $dir"
  if [[ -n "$(as_owner "$dir" git -C "$dir" status --porcelain)" ]]; then
    warn "Working tree is dirty; pulling anyway may conflict:"
    as_owner "$dir" git -C "$dir" status --porcelain | sed 's/^/      /'
  fi
  as_owner "$dir" git -C "$dir" fetch --all --prune && as_owner "$dir" git -C "$dir" pull --ff-only || {
    err "git pull failed (not a fast-forward?). Resolve by hand and re-run."
    exit 1
  }
  make_executable "$dir"
  do_build "$env"

  if systemctl is-active --quiet "$(unit "$env")"; then
    systemctl restart "$(unit "$env")" && ok "Restarted novaGround@$env on the new build."
  fi
  ok "Update complete."
}

resolve_release_commit() {
  # tag (or "stable") -> novaGround commit, via the Nova Console's release index.
  local which="${1:-stable}"
  local console; console="$(console_url "$(read_var "$(env_file prod)" NOVA_CONSOLE)")"
  if [[ -z "$console" ]]; then
    echo "no NOVA_CONSOLE in $(env_file prod) to ask" >&2
    return 1
  fi
  python3 "$PI_TOOLS" release "$console" "$which"
}

do_deploy() {
  need_root
  if [[ -n "$ENVIRONMENT" && "$ENVIRONMENT" != "prod" ]]; then
    err "deploy always targets prod ($PROD_DIR); dev is updated with: sudo $0 update --env dev"; exit 1
  fi
  assert_checkout
  local dir="$PROD_DIR"
  [[ -d "$dir" ]] || { err "Missing prod repo: $dir"; exit 1; }

  local commit label
  if [[ "$POSITIONAL" =~ ^[0-9a-f]{7,40}$ ]]; then
    commit="$POSITIONAL"
    label="${RELEASE_LABEL:-$POSITIONAL}"
  else
    step "Looking up ${POSITIONAL:-the newest stable release} on the ops PC"
    local resolved
    resolved="$(resolve_release_commit "$POSITIONAL")" || {
      err "Pass the commit instead (Nova.ps1 deploy prints it):  sudo $0 deploy <commit>"; exit 1; }
    label="${resolved%% *}"
    commit="${resolved##* }"
  fi

  g() { as_owner "$dir" git -c "safe.directory=$dir" -C "$@"; }

  if [[ -n "$(g "$dir" status --porcelain --untracked-files=no)" ]]; then
    err "$dir has local changes to tracked files. Settings belong in $ENV_DIR;"
    err "stash or discard these (git -C $dir stash), then deploy again:"
    g "$dir" status --porcelain --untracked-files=no | sed 's/^/      /' >&2
    exit 1
  fi

  step "Deploying novaGround $label (${commit:0:7}) to $dir"
  if ! g "$dir" cat-file -e "${commit}^{commit}" 2>/dev/null; then
    g "$dir" fetch origin >/dev/null 2>&1 || warn "fetch from origin failed (offline?)"
  fi
  if ! g "$dir" cat-file -e "${commit}^{commit}" 2>/dev/null && [[ -d "$DEV_DIR" ]]; then
    # Offline: the release was tested from the dev checkout, so it has it.
    g "$dir" fetch "$DEV_DIR" '+refs/heads/*:refs/remotes/nova-dev/*' '+HEAD:refs/remotes/nova-dev/HEAD' >/dev/null 2>&1
  fi
  g "$dir" cat-file -e "${commit}^{commit}" 2>/dev/null || {
    err "Commit $commit is not available from origin or from $DEV_DIR."; exit 1; }

  # Units and tooling come from this (dev) checkout: the tooling that was just
  # tested, which also knows how to move a Pi off the old single unit.
  setup_units

  local e
  for e in prod dev; do
    systemctl stop "$(unit "$e")" 2>/dev/null
  done
  ok "stopped novaGround"
  g "$dir" checkout -q --detach "$commit" || { err "checkout failed"; exit 1; }
  ok "checked out $(g "$dir" log -1 --format='%h %s')"
  # The checkout rewrites changed scripts with git's mode, which older
  # releases stored without the executable bit.
  make_executable "$dir"

  do_build prod

  mkdir -p "$ENV_DIR"
  printf 'release=%s\ncommit=%s\ndeployed=%s\n' "$label" "$(g "$dir" rev-parse HEAD)" "$(date -Iseconds)" > "$DEPLOYED_FILE"

  if [[ -f "$(env_file prod)" ]]; then
    systemctl start "$(unit prod)" && ok "novaGround@prod started"
  else
    warn "No prod settings yet ($(env_file prod)), so novaGround@prod was not started."
    warn "Run '.\\ops\\Nova.ps1 use prod' on the ops PC to write them and start it."
  fi
  report_now
  ok "Prod is on novaGround $label."
}

do_doctor() {
  echo
  step "novaGround prerequisites"
  check() {
    if [[ "$2" == "true" ]]; then printf "${GREEN}    %-26s OK    %s${RESET}\n" "$1" "${3:-}"
    else printf "${RED}    %-26s FAIL  %s${RESET}\n" "$1" "${3:-}"; fi
  }
  local tool e f target name backend broker console
  for tool in meson ninja clang git tmux python3; do
    if command -v "$tool" >/dev/null 2>&1; then check "$tool" true; else check "$tool" false "not installed"; fi
  done

  [[ -d "$PROD_DIR" ]] && check "prod repo" true "$PROD_DIR" || check "prod repo" false "$PROD_DIR"
  [[ -d "$DEV_DIR" ]]  && check "dev repo"  true "$DEV_DIR"  || check "dev repo"  false "$DEV_DIR"

  target="$(read_var "$PI_ENV" NOVA_TARGET)"
  name="$(read_var "$PI_ENV" NOVA_PI_NAME)"
  [[ -n "$target" ]] && check "this Pi" true "$name runs $target ($PI_ENV)" \
    || check "this Pi" false "no $PI_ENV - run: sudo $0 install"
  [[ -f "$TEMPLATE_UNIT" ]] && check "novaGround@ units" true "" || check "novaGround@ units" false "run: sudo $0 install"
  [[ -f "$LEGACY_UNIT" ]] && check "old novaGround.service" false "still installed - run: sudo $0 install" \
    || check "old novaGround.service" true "gone"
  [[ "$(systemctl is-enabled "$(unit prod)" 2>/dev/null)" == "enabled" ]] \
    && check "prod starts on boot" true "" || check "prod starts on boot" false "run: sudo $0 install"
  systemctl is-active --quiet "$REPORT_TIMER" && check "report timer" true "" \
    || check "report timer" false "run: sudo $0 install"
  # The hardware watchdog: set in the files, and running now (after a reboot).
  local cfg
  if cfg="$(boot_config)" && grep -qxF "dtparam=watchdog=on" "$cfg"; then
    check "watchdog in config.txt" true "dtparam=watchdog=on"
  else
    check "watchdog in config.txt" false "run: sudo $0 install"
  fi
  if [[ -f "$SYSTEMD_CONF" ]] && grep -qxF "RuntimeWatchdogSec=${RUNTIME_WATCHDOG_SEC}" "$SYSTEMD_CONF"; then
    check "systemd RuntimeWatchdogSec" true "${RUNTIME_WATCHDOG_SEC}s"
  else
    check "systemd RuntimeWatchdogSec" false "run: sudo $0 install"
  fi
  local runtime
  runtime="$(systemctl show -p RuntimeWatchdogUSec --value 2>/dev/null)"
  if [[ -n "$runtime" && "$runtime" != "0" && "$runtime" != "infinity" && "$runtime" != "off" ]]; then
    check "hardware watchdog running" true "systemd pings it (${runtime})"
  else
    check "hardware watchdog running" false "not yet: reboot after install (${runtime:-unknown})"
  fi

  for e in prod dev; do
    f="$(env_file "$e")"
    if [[ -f "$f" ]]; then
      backend="$(read_var "$f" NOVA_BACKEND)"
      check "$e settings" true "backend $backend"
      if http_ok "http://${backend}/health"; then check "$e backend answers" true "$backend"
      else check "$e backend answers" false "$backend (fine if that stack isn't running)"; fi
    else
      check "$e settings" false "no $f - run '.\\ops\\Nova.ps1 use $e' on the ops PC"
    fi
    if [[ -n "$target" ]]; then
      [[ -x "$(repo_dir "$e")/build/${target}" ]] && check "$e binary" true "build/${target}" \
        || check "$e binary" false "run: $0 build --env $e"
    fi
  done

  broker="$(read_var "$(env_file prod)" NOVA_BROKER)"
  if [[ -n "$broker" ]]; then
    if ping -c 1 -W 1 "${broker%%:*}" >/dev/null 2>&1; then check "ops PC reachable" true "${broker%%:*}"
    else check "ops PC reachable" false "${broker%%:*}"; fi
  fi
  console="$(console_url "$(read_var "$(env_file prod)" NOVA_CONSOLE)")"
  if [[ -n "$console" ]]; then
    http_ok "${console}/api/health" && check "Nova Console" true "$console" || check "Nova Console" false "$console"
  fi

  local ip
  ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
  check "this Pi's IP" true "${ip:-unknown}"
  echo
}

do_uninstall() {
  need_root
  step "Removing the novaGround services"
  local e
  for e in prod dev; do
    systemctl disable --now "$(unit "$e")" >/dev/null 2>&1
  done
  systemctl disable --now "$REPORT_TIMER" >/dev/null 2>&1
  systemctl disable --now novaGround.service >/dev/null 2>&1
  rm -f "$TEMPLATE_UNIT" "${UNIT_DIR}/${REPORT_SERVICE}" "${UNIT_DIR}/${REPORT_TIMER}" "$LEGACY_UNIT" "$TOOL" "${TOOL_DIR}/nova-pi-tools.py"
  systemctl daemon-reload
  ok "Services removed. $ENV_DIR and the repos were left in place."
}

case "$ACTION" in
  install)   do_install ;;
  uninstall) do_uninstall ;;
  configure) do_configure ;;
  use)       do_use ;;
  build)     do_build ;;
  start)     do_start ;;
  stop)      do_stop ;;
  restart)   do_restart ;;
  status)    do_status ;;
  report)    do_report ;;
  metrics)   do_metrics ;;
  logs)      do_logs ;;
  update)    do_update ;;
  deploy)    do_deploy ;;
  doctor)    do_doctor ;;
  ""|-h|--help) usage ;;
  *) err "Unknown action: $ACTION"; usage; exit 1 ;;
esac
