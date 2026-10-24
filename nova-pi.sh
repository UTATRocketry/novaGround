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
#      sudo ./nova-pi.sh configure --env prod|dev --broker <host:port> --backend <host:port> [--console <url>]
#           ./nova-pi.sh build [--env prod|dev]
#      sudo ./nova-pi.sh start | stop | restart [--env prod|dev]
#           ./nova-pi.sh status [--json] | logs [--env prod|dev] [-f] | doctor | report
#      sudo ./nova-pi.sh update [--env prod|dev]
#      sudo ./nova-pi.sh deploy [<tag>|<commit>] [--release <label>]
#      sudo ./nova-pi.sh uninstall
#
#  There is one systemd instance per environment, like the PC's two stacks:
#  novaGround@prod (enabled, starts on boot) and novaGround@dev (started only
#  by `use dev`). Only one runs at a time.
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

PROD_DIR="/home/admin/prod/novaGround"
DEV_DIR="/home/admin/dev/novaGround"
UNIT_DIR="/etc/systemd/system"
TEMPLATE_UNIT="${UNIT_DIR}/novaGround@.service"
REPORT_SERVICE="nova-pi-report.service"
REPORT_TIMER="nova-pi-report.timer"
# The single unit and env file used before novaGround@prod / @dev.
LEGACY_UNIT="${UNIT_DIR}/novaGround.service"
ENV_DIR="/etc/nova"
LEGACY_ENV="${ENV_DIR}/novaGround.env"
# Which Pi this is and which binary it runs. Owned by this Pi.
PI_ENV="${ENV_DIR}/pi.env"
# What prod is running, written by `deploy`.
DEPLOYED_FILE="${ENV_DIR}/deployed"
STATE_ROOT="/var/lib/nova"
# A copy of this script that outlives any checkout, run by the report timer.
TOOL_DIR="/usr/local/lib/nova"
TOOL="${TOOL_DIR}/nova-pi.sh"
REPORT_VERSION=1
TARGETS="novaGround novaThermo novaMock"

HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; RESET=$'\033[0m'
step() { echo "${CYAN}==> $*${RESET}"; }
ok()   { echo "${GREEN}    $*${RESET}"; }
warn() { echo "${YELLOW}    $*${RESET}"; }
err()  { echo "${RED}    $*${RESET}" >&2; }

usage() { sed -n '3,/^# =\{10\}/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

need_root() {
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

report_now() { systemctl start --no-block "$REPORT_SERVICE" >/dev/null 2>&1 || true; }

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
BROKER=""; BACKEND=""; CONSOLE=""; RELEASE_LABEL=""
FOLLOW="false"; JSON="false"

need_value() { if [[ $# -lt 2 || -z "$2" ]]; then err "$1 needs a value"; exit 1; fi; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env)       need_value "$@"; ENVIRONMENT="$2"; shift 2 ;;
    --target)    need_value "$@"; TARGET="$2"; shift 2 ;;
    --name)      need_value "$@"; PI_NAME="$2"; shift 2 ;;
    --broker)    need_value "$@"; BROKER="$2"; shift 2 ;;
    --backend)   need_value "$@"; BACKEND="$2"; shift 2 ;;
    --console)   need_value "$@"; CONSOLE="$2"; shift 2 ;;
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

# ── settings files ───────────────────────────────────────────────────────────

# write_env_file <env> <broker> <backend> <console> <where the values came from>
write_env_file() {
  local f; f="$(env_file "$1")"
  mkdir -p "$ENV_DIR"
  cat > "$f" <<EOF
# novaGround settings for the $1 environment ($5).
# Nova.ps1 on the ops PC rewrites this file on every 'use', 'deploy' and
# 'get-changes', from ops/nova.settings.ps1: change the settings there.
NOVA_BROKER=$2
NOVA_BACKEND=$3
NOVA_CONSOLE=$4
EOF
  chmod 644 "$f"
}

# /etc/nova/pi.env: --name/--target win, then the existing file, then the old
# single env file. Asks for the target only when there's nothing to go on.
ensure_pi_env() {
  local target name extra
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

  if [[ -f "$PI_ENV" ]]; then extra="$(read_var "$PI_ENV" NOVA_EXTRA_ARGS)"
  else extra="$(read_var "$LEGACY_ENV" NOVA_EXTRA_ARGS)"; fi

  if [[ -f "$PI_ENV" && "$(read_var "$PI_ENV" NOVA_TARGET)" == "$target" && \
        "$(read_var "$PI_ENV" NOVA_PI_NAME)" == "$name" ]]; then
    return
  fi
  mkdir -p "$ENV_DIR"
  cat > "$PI_ENV" <<EOF
# Which Pi this is and which binary it runs. Written by nova-pi.sh install;
# change it with:  sudo ./nova-pi.sh install --name <name> --target <binary>
# NOVA_PI_NAME must match this Pi's name in ops/nova.settings.ps1 on the ops PC.
NOVA_PI_NAME=${name}
NOVA_TARGET=${target}

# Extra flags for this Pi's binary, e.g. "--verbosity 2 --publish-ms 100".
NOVA_EXTRA_ARGS=${extra}
EOF
  chmod 644 "$PI_ENV"
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
      write_env_file prod "$(read_var "$LEGACY_ENV" NOVA_BROKER)" "$(read_var "$LEGACY_ENV" NOVA_BACKEND)" "" \
        "carried over from $LEGACY_ENV"
      ok "Carried the old broker and backend over into $(env_file prod)"
    fi
    mv "$LEGACY_ENV" "${LEGACY_ENV}.old"
    ok "Kept the old settings as ${LEGACY_ENV}.old"
  fi
}

# Units, the report timer and the tool copy, from the checkout this runs from.
setup_units() {
  assert_checkout
  for f in "novaGround@.service" "$REPORT_SERVICE" "$REPORT_TIMER"; do
    [[ -f "${HERE}/$f" ]] || { err "${HERE} has no $f. Update this checkout first."; exit 1; }
  done
  mkdir -p "$ENV_DIR" "$STATE_ROOT" "$TOOL_DIR"
  ensure_pi_env
  migrate_legacy

  install -m 644 "${HERE}/novaGround@.service" "$TEMPLATE_UNIT"
  install -m 644 "${HERE}/${REPORT_SERVICE}" "${UNIT_DIR}/${REPORT_SERVICE}"
  install -m 644 "${HERE}/${REPORT_TIMER}" "${UNIT_DIR}/${REPORT_TIMER}"
  install -m 755 "${HERE}/nova-pi.sh" "$TOOL"
  chmod +x "${PROD_DIR}/start.sh" "${PROD_DIR}/run.sh" "${DEV_DIR}/start.sh" "${DEV_DIR}/run.sh" 2>/dev/null || true

  systemctl daemon-reload
  # Prod comes back on boot; dev never does, exactly like the PC's stacks.
  systemctl enable "$(unit prod)" >/dev/null 2>&1
  systemctl disable "$(unit dev)" >/dev/null 2>&1 || true
  systemctl enable --now "$REPORT_TIMER" >/dev/null 2>&1
  ok "Installed novaGround@.service, the report timer and $TOOL"
}

# ── actions ──────────────────────────────────────────────────────────────────

do_build() {
  local env="${1:-${ENVIRONMENT:-prod}}"
  local dir; dir="$(repo_dir "$env")"
  [[ -d "$dir" ]] || { err "Missing repo: $dir"; exit 1; }

  step "Building novaGround ($env) in $dir"
  # Older deploys built as root; hand the build folder back to the owner.
  if [[ $EUID -eq 0 && -d "${dir}/build" ]]; then chown -R "$(owner_of "$dir"):" "${dir}/build"; fi
  if [[ ! -d "${dir}/build" ]]; then
    step "Setting up the meson build directory"
    as_owner "$dir" bash -c 'cd "$1" && CC=clang CXX=clang++ meson setup build' _ "$dir" \
      || { err "meson setup failed"; exit 1; }
  fi
  as_owner "$dir" meson compile -C "${dir}/build" || { err "build failed"; exit 1; }
  ok "Build complete."
  ls -1 "${dir}/build/" 2>/dev/null | grep -E '^nova' | sed 's/^/    /'
}

do_install() {
  need_root
  step "Installing novaGround from $HERE"
  setup_units

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
  [[ -n "$ENVIRONMENT" && -n "$BROKER" && -n "$BACKEND" ]] || {
    err "Usage: sudo $0 configure --env prod|dev --broker <host:port> --backend <host:port> [--console <url>]"
    err "Normally Nova.ps1 writes this for you ('.\\ops\\Nova.ps1 use $ENVIRONMENT' on the ops PC)."
    exit 1
  }
  local console="${CONSOLE:-$(read_var "$(env_file "$ENVIRONMENT")" NOVA_CONSOLE)}"
  write_env_file "$ENVIRONMENT" "$BROKER" "$BACKEND" "$console" "written by hand with nova-pi.sh configure"
  ok "Wrote $(env_file "$ENVIRONMENT"): backend $BACKEND"
  if systemctl is-active --quiet "$(unit "$ENVIRONMENT")"; then
    systemctl restart "$(unit "$ENVIRONMENT")" && ok "Restarted novaGround@$ENVIRONMENT to pick it up."
  fi
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
  PROD_DIR="$PROD_DIR" DEV_DIR="$DEV_DIR" ENV_DIR="$ENV_DIR" PI_ENV="$PI_ENV" \
  STATE_ROOT="$STATE_ROOT" DEPLOYED_FILE="$DEPLOYED_FILE" LEGACY_UNIT="$LEGACY_UNIT" \
  REPORT_VERSION="$REPORT_VERSION" python3 - "$1" <<'PY'
import json, os, pwd, socket, subprocess, sys, time, urllib.error, urllib.request

mode = sys.argv[1]  # json | human | report
E = os.environ
DIRS = {"prod": E["PROD_DIR"], "dev": E["DEV_DIR"]}


def run(cmd, timeout=5):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout.strip()
    except Exception:
        return ""


def read_env(path):
    if not os.path.isfile(path):
        return None
    out = {}
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip().strip('"').strip("'")
    return out


def read_json(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:
        return None


def as_owner(cmd, path):
    if os.geteuid() == 0:
        try:
            owner = pwd.getpwuid(os.stat(path).st_uid).pw_name
        except Exception:
            owner = None
        if owner and owner != "root":
            return ["sudo", "-u", owner] + cmd
    return cmd


def git(d, *args):
    return run(["git", "-c", "safe.directory=" + d, "-C", d] + list(args))


def health(backend):
    """Does this environment's backend answer, seen from the Pi?"""
    if not backend:
        return None
    base = backend if "://" in backend else "http://" + backend
    try:
        with urllib.request.urlopen(base.rstrip("/") + "/health", timeout=2) as resp:
            return {"ok": 200 <= resp.status < 400, "detail": "HTTP %d" % resp.status}
    except urllib.error.HTTPError as exc:
        return {"ok": False, "detail": "HTTP %d" % exc.code}
    except Exception as exc:
        reason = getattr(exc, "reason", exc)
        return {"ok": False, "detail": reason if isinstance(reason, str) else type(reason).__name__}


pi = read_env(E["PI_ENV"]) or {}
snap = {
    "version": int(E["REPORT_VERSION"]),
    "name": pi.get("NOVA_PI_NAME") or None,
    "target": pi.get("NOVA_TARGET") or None,
    "hostname": socket.gethostname(),
    "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
    "legacyUnit": os.path.isfile(E["LEGACY_UNIT"]),
    "active": None,
    "bench": None,
    "deployed": None,
    "instances": {},
}

for name in run(as_owner(["tmux", "list-sessions", "-F", "#S"], DIRS["prod"])).splitlines():
    if name in ("NovaGround-dev", "NovaGround-prod"):
        snap["bench"] = name.split("-", 1)[1]
    elif name == "NovaGround" and not snap["bench"]:
        snap["bench"] = "unknown"

console = None
for env, d in DIRS.items():
    cfg = read_env(os.path.join(E["ENV_DIR"], "novaGround-%s.env" % env))
    unit = "novaGround@%s.service" % env
    state = run(["systemctl", "is-active", unit]) or "unknown"
    inst = {
        "state": state,
        "enabled": run(["systemctl", "is-enabled", unit]) or "unknown",
        "configured": cfg is not None,
        "broker": (cfg or {}).get("NOVA_BROKER") or None,
        "backend": (cfg or {}).get("NOVA_BACKEND") or None,
        "checkout": None,
        "built": None,
        "lastUpload": read_json(os.path.join(E["STATE_ROOT"], env, "last_upload.json")),
    }
    inst["backendHealth"] = health(inst["backend"])
    if os.path.isdir(os.path.join(d, ".git")):
        branch = git(d, "rev-parse", "--abbrev-ref", "HEAD")
        dirty = git(d, "status", "--porcelain", "--untracked-files=no")
        inst["checkout"] = {
            "dir": d,
            "commit": git(d, "rev-parse", "HEAD") or None,
            "branch": "(detached)" if branch == "HEAD" else (branch or None),
            "dirty": len([l for l in dirty.splitlines() if l.strip()]),
        }
        if snap["target"]:
            inst["built"] = os.access(os.path.join(d, "build", snap["target"]), os.X_OK)
    if state == "active":
        snap["active"] = env
    if not console and cfg and cfg.get("NOVA_CONSOLE") and env == "prod":
        console = cfg["NOVA_CONSOLE"]
    snap["instances"][env] = inst
if not console:
    console = ((read_env(os.path.join(E["ENV_DIR"], "novaGround-dev.env")) or {}).get("NOVA_CONSOLE")) or None
snap["console"] = console

if snap["legacyUnit"] and run(["systemctl", "is-active", "novaGround.service"]) == "active":
    snap["active"] = "legacy"

deployed = read_env(E["DEPLOYED_FILE"])
if deployed:
    snap["deployed"] = {"release": deployed.get("release"), "commit": deployed.get("commit"),
                        "time": deployed.get("deployed")}

if mode == "json":
    print(json.dumps(snap, indent=2))
    sys.exit(0)

if mode == "report":
    if not snap["name"]:
        sys.exit("no NOVA_PI_NAME in %s - run: sudo ./nova-pi.sh install" % E["PI_ENV"])
    if not console:
        sys.exit("no NOVA_CONSOLE in the env files yet - Nova.ps1 writes it on 'use' or 'deploy'")
    req = urllib.request.Request(console.rstrip("/") + "/api/pi-report",
                                 data=json.dumps(snap).encode("utf-8"),
                                 headers={"Content-Type": "application/json"}, method="POST")
    try:
        urllib.request.urlopen(req, timeout=4).read()
    except Exception as exc:  # noqa: BLE001
        sys.exit("could not report to %s: %s" % (console, exc))
    sys.exit(0)

# human
G, Y, R, C, X = "\033[32m", "\033[33m", "\033[31m", "\033[36m", "\033[0m"
print()
print("novaGround on %s - %s (%s)" % (snap["hostname"], snap["name"] or "unnamed Pi", snap["target"] or "no target"))
print("-" * 70)
active = snap["active"]
if active == "legacy":
    print(Y + "    running      the old novaGround.service - migrate: sudo ./nova-pi.sh install" + X)
elif active:
    print(G + "    running      novaGround@%s" % active + X)
else:
    print(R + "    running      nothing" + X)
if snap["bench"]:
    print(Y + "    bench        a run.sh session (%s) is running" % snap["bench"] + X)
if snap["deployed"]:
    d = snap["deployed"]
    print("    release      %s (%s, deployed %s)" % (d["release"], (d["commit"] or "")[:7], d["time"]))
for env, inst in snap["instances"].items():
    co = inst["checkout"] or {}
    code = "%s %s" % ((co.get("commit") or "-------")[:7], co.get("branch") or "")
    if co.get("dirty"):
        code += " +%d uncommitted" % co["dirty"]
    if inst["built"] is False:
        code += " (not built)"
    colour = G if inst["state"] == "active" else ""
    print(colour + "    %-12s %-9s %-10s %s" % (env, inst["state"], "(" + inst["enabled"] + ")", code) + (X if colour else ""))
    if not inst["configured"]:
        print(Y + "                 no settings file - Nova.ps1 writes it on 'use %s'" % env + X)
    else:
        h = inst["backendHealth"] or {}
        hc = G if h.get("ok") else R
        print("                 backend %s %s%s%s" % (inst["backend"], hc, "answers" if h.get("ok") else "NO ANSWER (%s)" % h.get("detail"), X))
    up = inst["lastUpload"]
    if up:
        uc = G if up.get("ok") else R
        print("                 last upload %s %s%s%s" % (up.get("time"), uc, "OK" if up.get("ok") else "FAILED", X))
print("    console      %s" % (console or "(not set yet)"))
print()
PY
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
  do_build "$env"

  if systemctl is-active --quiet "$(unit "$env")"; then
    systemctl restart "$(unit "$env")" && ok "Restarted novaGround@$env on the new build."
  fi
  ok "Update complete."
}

resolve_release_commit() {
  # tag (or "stable") -> novaGround commit, via the Nova Console's release index.
  local which="${1:-stable}"
  local console; console="$(read_var "$(env_file prod)" NOVA_CONSOLE)"
  if [[ -z "$console" ]]; then
    echo "no NOVA_CONSOLE in $(env_file prod) to ask" >&2
    return 1
  fi
  python3 - "$console" "$which" <<'PY'
import json, sys, urllib.request
console, which = sys.argv[1], sys.argv[2]
try:
    with urllib.request.urlopen(f"{console}/api/releases/{which}", timeout=5) as resp:
        release = json.load(resp)
except Exception as exc:  # noqa: BLE001
    sys.exit(f"could not ask {console} about '{which}': {exc}")
commit = ((release.get("pins") or {}).get("pi/novaGround") or {}).get("commit")
if not commit:
    sys.exit(f"release '{which}' pins no novaGround commit")
print(release.get("tag", which), commit)
PY
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
  console="$(read_var "$(env_file prod)" NOVA_CONSOLE)"
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
  rm -f "$TEMPLATE_UNIT" "${UNIT_DIR}/${REPORT_SERVICE}" "${UNIT_DIR}/${REPORT_TIMER}" "$LEGACY_UNIT" "$TOOL"
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
  logs)      do_logs ;;
  update)    do_update ;;
  deploy)    do_deploy ;;
  doctor)    do_doctor ;;
  ""|-h|--help) usage ;;
  *) err "Unknown action: $ACTION"; usage; exit 1 ;;
esac
