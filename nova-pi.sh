#!/usr/bin/env bash
# ============================================================================
#  nova-pi.sh — install, build, start, stop, restart, update and inspect
#  novaGround on the Raspberry Pi.
#
#  Mirrors ops/Nova.ps1 on the Windows ops PC so operators learn one set of
#  verbs for the whole ground station.
#
#  Usage:
#      ./nova-pi.sh install [--env prod|dev] [--target novaGround|novaThermo|novaMock]
#      ./nova-pi.sh build   [--env prod|dev]
#      ./nova-pi.sh start | stop | restart | status | logs | update | doctor
#      ./nova-pi.sh deploy [<tag>|<commit>]
#      ./nova-pi.sh uninstall
#
#  deploy puts a pinned Nova release into prod: with a tag (v2026.10.04) it
#  asks the Nova Console on the ops PC which novaGround commit that release
#  pins; with no argument it uses the newest stable release. The commit comes
#  from GitHub, or - offline - from the dev checkout it was tested in.
#
#  Production runs under systemd (Restart=always, enabled at boot). Dev is run
#  by hand with ./run.sh, exactly as dev on the Windows PC is Manual-start.
# ============================================================================
set -uo pipefail

PROD_DIR="/home/admin/prod/novaGround"
DEV_DIR="/home/admin/dev/novaGround"
SERVICE_NAME="novaGround"
UNIT_PATH="/etc/systemd/system/${SERVICE_NAME}.service"
ENV_DIR="/etc/nova"
ENV_FILE="${ENV_DIR}/novaGround.env"
# What prod is running, written by `deploy`.
DEPLOYED_FILE="${ENV_DIR}/deployed"

# Defaults written into the env file on first install.
DEFAULT_BROKER="192.168.137.1"
DEFAULT_BACKEND="192.168.137.1:8000"
DEFAULT_TARGET="novaGround"
# The Nova Console on the ops PC serves the release index (/api/releases).
DEFAULT_CONSOLE="http://192.168.137.1:8080"

ENVIRONMENT="prod"
DEPLOY_REF=""
RELEASE_LABEL=""
TARGET=""
FOLLOW="false"

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; RESET=$'\033[0m'
step() { echo "${CYAN}==> $*${RESET}"; }
ok()   { echo "${GREEN}    $*${RESET}"; }
warn() { echo "${YELLOW}    $*${RESET}"; }
err()  { echo "${RED}    $*${RESET}" >&2; }

usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; }

need_root() {
  if [[ $EUID -ne 0 ]]; then
    err "This action needs root. Re-run with sudo:  sudo $0 $ACTION"
    exit 1
  fi
}

repo_dir() {
  if [[ "$ENVIRONMENT" == "dev" ]]; then echo "$DEV_DIR"; else echo "$PROD_DIR"; fi
}

# ── argument parsing ─────────────────────────────────────────────────────────

ACTION="${1:-}"
shift || true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --env)     ENVIRONMENT="$2"; shift 2 ;;
    --target)  TARGET="$2"; shift 2 ;;
    -f|--follow) FOLLOW="true"; shift ;;
    --release) RELEASE_LABEL="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) err "Unknown argument: $1"; usage; exit 1 ;;
    *)
      if [[ "$ACTION" == "deploy" && -z "$DEPLOY_REF" ]]; then DEPLOY_REF="$1"; shift
      else err "Unknown argument: $1"; usage; exit 1; fi ;;
  esac
done

if [[ "$ENVIRONMENT" != "prod" && "$ENVIRONMENT" != "dev" ]]; then
  err "--env must be prod or dev"; exit 1
fi

# ── actions ──────────────────────────────────────────────────────────────────

do_build() {
  local dir; dir="$(repo_dir)"
  [[ -d "$dir" ]] || { err "Missing repo: $dir"; exit 1; }

  step "Building novaGround in $dir"
  if [[ ! -d "${dir}/build" ]]; then
    step "Setting up meson build directory"
    ( cd "$dir" && CC=clang CXX=clang++ meson setup build ) || { err "meson setup failed"; exit 1; }
  fi
  ( cd "$dir" && meson compile -C build ) || { err "build failed"; exit 1; }
  ok "Build complete."
  ls -1 "${dir}/build/" 2>/dev/null | grep -E '^nova' | sed 's/^/    /'
}

do_install() {
  need_root
  local dir="$PROD_DIR"
  [[ -d "$dir" ]] || { err "Missing prod repo: $dir"; exit 1; }

  step "Installing the $SERVICE_NAME service"

  # Config file first, so the unit has something to read.
  mkdir -p "$ENV_DIR"
  if [[ ! -f "$ENV_FILE" ]]; then
    local target="${TARGET:-$DEFAULT_TARGET}"
    cat > "$ENV_FILE" <<EOF
# novaGround runtime configuration. Read by both the systemd service and
# ./run.sh, so the bench session and production agree.
# Change a value here and run:  sudo systemctl restart novaGround

# Which binary to run: novaGround | novaThermo | novaMock
NOVA_TARGET=${target}

# Ops PC (Nova-Server) addresses.
NOVA_BROKER=${DEFAULT_BROKER}
NOVA_BACKEND=${DEFAULT_BACKEND}

# Optional FAS RS-422 link. Leave empty when no adapter is fitted — the service
# starts without it rather than failing. Example: /dev/ttyUSB0
NOVA_FAS_PORT=
NOVA_FAS_BAUD=460800

# Extra flags appended verbatim, e.g. "--verbosity 2 --publish-ms 100"
NOVA_EXTRA_ARGS=
EOF
    chmod 644 "$ENV_FILE"
    ok "Wrote $ENV_FILE"
  else
    ok "Kept existing $ENV_FILE (delete it to regenerate defaults)"
    if [[ -n "$TARGET" ]]; then
      sed -i "s/^NOVA_TARGET=.*/NOVA_TARGET=${TARGET}/" "$ENV_FILE"
      ok "Set NOVA_TARGET=${TARGET}"
    fi
  fi

  chmod +x "${dir}/start.sh" "${dir}/run.sh" "${dir}/nova-pi.sh" 2>/dev/null || true

  if [[ ! -x "${dir}/build/$(grep -oP '^NOVA_TARGET=\K.*' "$ENV_FILE")" ]]; then
    warn "No built binary yet — run: $0 build"
  fi

  install -m 644 "${dir}/novaGround.service" "$UNIT_PATH"
  ok "Installed $UNIT_PATH"

  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME"
  ok "Service enabled — it will start automatically on boot."
  echo
  ok "Start it now with:  sudo $0 start"
}

do_uninstall() {
  need_root
  step "Removing the $SERVICE_NAME service"
  systemctl stop "$SERVICE_NAME" 2>/dev/null
  systemctl disable "$SERVICE_NAME" 2>/dev/null
  rm -f "$UNIT_PATH"
  systemctl daemon-reload
  ok "Service removed. $ENV_FILE and the repo were left in place."
}

do_start()   { need_root; step "Starting $SERVICE_NAME";   systemctl start "$SERVICE_NAME"   && ok "started"; }
do_stop()    { need_root; step "Stopping $SERVICE_NAME";   systemctl stop "$SERVICE_NAME"    && ok "stopped"; }
do_restart() { need_root; step "Restarting $SERVICE_NAME"; systemctl restart "$SERVICE_NAME" && ok "restarted"; }

do_status() {
  echo
  echo "novaGround — status"
  echo "------------------------------------------------------------------"

  if [[ ! -f "$UNIT_PATH" ]]; then
    err "service not installed (run: sudo $0 install)"
  else
    local state enabled
    state="$(systemctl is-active "$SERVICE_NAME" 2>/dev/null)"
    enabled="$(systemctl is-enabled "$SERVICE_NAME" 2>/dev/null)"
    if [[ "$state" == "active" ]]; then
      ok "service      active (${enabled})"
    else
      err "service      ${state} (${enabled})"
    fi
    local since
    since="$(systemctl show -p ActiveEnterTimestamp --value "$SERVICE_NAME" 2>/dev/null)"
    [[ -n "$since" ]] && echo "    since        $since"
  fi
  if [[ -f "$DEPLOYED_FILE" ]]; then
    echo "    release      $(grep -oP '^release=\K.*' "$DEPLOYED_FILE") ($(grep -oP '^commit=\K.{7}' "$DEPLOYED_FILE"), $(grep -oP '^deployed=\K.*' "$DEPLOYED_FILE"))"
  fi

  if [[ -f "$ENV_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
    echo "    target       ${NOVA_TARGET:-?}"
    echo "    broker       ${NOVA_BROKER:-?}"
    echo "    backend      ${NOVA_BACKEND:-?}"
    echo "    fas port     ${NOVA_FAS_PORT:-<none>}"
  else
    warn "no $ENV_FILE"
  fi

  echo
  echo "  connectivity"
  local broker_host="${NOVA_BROKER:-$DEFAULT_BROKER}"
  if ping -c 1 -W 1 "$broker_host" >/dev/null 2>&1; then
    ok "ops PC       reachable ($broker_host)"
  else
    err "ops PC       NO REPLY ($broker_host)"
  fi
  if command -v nc >/dev/null 2>&1; then
    if nc -z -w 2 "$broker_host" 1883 >/dev/null 2>&1; then
      ok "broker 1883  open"
    else
      err "broker 1883  closed"
    fi
  fi

  echo
  echo "  recent log"
  journalctl -u "$SERVICE_NAME" -n 8 --no-pager 2>/dev/null | sed 's/^/    /'
  echo
}

do_logs() {
  if [[ "$FOLLOW" == "true" ]]; then
    journalctl -u "$SERVICE_NAME" -f
  else
    journalctl -u "$SERVICE_NAME" -n 100 --no-pager
  fi
}

do_update() {
  need_root
  local dir; dir="$(repo_dir)"
  [[ -d "$dir" ]] || { err "Missing repo: $dir"; exit 1; }

  step "Updating novaGround in $dir"
  local was_active="false"
  if systemctl is-active --quiet "$SERVICE_NAME"; then
    was_active="true"
    systemctl stop "$SERVICE_NAME"
    ok "stopped the service for the update"
  fi

  if [[ -n "$(cd "$dir" && git status --porcelain)" ]]; then
    warn "Working tree is dirty; pulling anyway may conflict:"
    ( cd "$dir" && git status --porcelain | sed 's/^/      /' )
  fi

  ( cd "$dir" && git fetch --all --prune && git pull --ff-only ) || {
    err "git pull failed (not a fast-forward?). Resolve by hand and re-run."
    exit 1
  }

  do_build

  # Reinstall in case the unit file itself changed in the pull.
  if [[ -f "${dir}/novaGround.service" && "$ENVIRONMENT" == "prod" ]]; then
    install -m 644 "${dir}/novaGround.service" "$UNIT_PATH"
    systemctl daemon-reload
    ok "Refreshed the unit file."
  fi

  if [[ "$was_active" == "true" ]]; then
    systemctl start "$SERVICE_NAME"
    ok "service restarted"
  fi
  ok "Update complete."
}

resolve_release_commit() {
  # tag (or "stable") -> novaGround commit, via the Nova Console's release index.
  local which="${1:-stable}"
  local console="${NOVA_CONSOLE_URL:-$DEFAULT_CONSOLE}"
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
  if [[ "$ENVIRONMENT" != "prod" ]]; then
    err "deploy always targets prod ($PROD_DIR); dev is updated with git directly."; exit 1
  fi
  local dir="$PROD_DIR"
  [[ -d "$dir" ]] || { err "Missing prod repo: $dir"; exit 1; }

  local commit label
  if [[ "$DEPLOY_REF" =~ ^[0-9a-f]{7,40}$ ]]; then
    commit="$DEPLOY_REF"
    label="${RELEASE_LABEL:-$DEPLOY_REF}"
  else
    step "Looking up ${DEPLOY_REF:-the newest stable release} on the ops PC"
    local resolved
    resolved="$(resolve_release_commit "$DEPLOY_REF")" || {
      err "Pass the commit instead (Nova.ps1 deploy prints it):  sudo $0 deploy <commit>"; exit 1; }
    label="${resolved%% *}"
    commit="${resolved##* }"
  fi

  # git runs as the checkout's owner: objects fetched as root would leave
  # root-owned files that break the next ordinary `git pull`.
  local owner; owner="$(stat -c %U "$dir")"
  g() { sudo -u "$owner" git -C "$@"; }

  if [[ -n "$(g "$dir" status --porcelain --untracked-files=no)" ]]; then
    err "$dir has local changes to tracked files. Settings belong in $ENV_FILE;"
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

  systemctl stop "$SERVICE_NAME" 2>/dev/null && ok "stopped the service"
  g "$dir" checkout -q --detach "$commit" || { err "checkout failed"; exit 1; }
  ok "checked out $(g "$dir" log -1 --format='%h %s')"

  ENVIRONMENT="prod" do_build

  if [[ -f "${dir}/novaGround.service" ]]; then
    install -m 644 "${dir}/novaGround.service" "$UNIT_PATH"
    systemctl daemon-reload
  fi

  mkdir -p "$ENV_DIR"
  printf 'release=%s\ncommit=%s\ndeployed=%s\n' "$label" "$(g "$dir" rev-parse HEAD)" "$(date -Iseconds)" > "$DEPLOYED_FILE"

  systemctl start "$SERVICE_NAME" && ok "service started"
  ok "Prod is running novaGround $label."
}

do_doctor() {
  echo
  step "novaGround prerequisites"
  check() {
    if [[ "$2" == "true" ]]; then printf "${GREEN}    %-24s OK    %s${RESET}\n" "$1" "${3:-}"
    else printf "${RED}    %-24s FAIL  %s${RESET}\n" "$1" "${3:-}"; fi
  }

  for tool in meson ninja clang git tmux; do
    if command -v "$tool" >/dev/null 2>&1; then check "$tool" true; else check "$tool" false "not installed"; fi
  done

  [[ -d "$PROD_DIR" ]] && check "prod repo" true "$PROD_DIR" || check "prod repo" false "$PROD_DIR"
  [[ -d "$DEV_DIR" ]]  && check "dev repo"  true "$DEV_DIR"  || check "dev repo"  false "$DEV_DIR"
  [[ -f "$ENV_FILE" ]] && check "config" true "$ENV_FILE" || check "config" false "run: sudo $0 install"
  [[ -f "$UNIT_PATH" ]] && check "unit installed" true "" || check "unit installed" false "run: sudo $0 install"

  local target="$DEFAULT_TARGET"
  [[ -f "$ENV_FILE" ]] && target="$(grep -oP '^NOVA_TARGET=\K.*' "$ENV_FILE" 2>/dev/null || echo "$DEFAULT_TARGET")"
  [[ -x "${PROD_DIR}/build/${target}" ]] && check "prod binary" true "build/${target}" \
    || check "prod binary" false "run: $0 build"

  local ip
  ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
  check "this Pi's IP" true "${ip:-unknown}"

  if ping -c 1 -W 1 "$DEFAULT_BROKER" >/dev/null 2>&1; then
    check "ops PC reachable" true "$DEFAULT_BROKER"
  else
    check "ops PC reachable" false "$DEFAULT_BROKER"
  fi

  if [[ -n "${NOVA_FAS_PORT:-}" ]]; then
    [[ -e "${NOVA_FAS_PORT}" ]] && check "FAS serial" true "${NOVA_FAS_PORT}" \
      || check "FAS serial" false "${NOVA_FAS_PORT} absent"
  fi
  echo
  echo "    serial devices present: $(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | tr '\n' ' ' || echo none)"
  echo
}

case "$ACTION" in
  install)   do_install ;;
  uninstall) do_uninstall ;;
  build)     do_build ;;
  start)     do_start ;;
  stop)      do_stop ;;
  restart)   do_restart ;;
  status)    do_status ;;
  logs)      do_logs ;;
  update)    do_update ;;
  deploy)    do_deploy ;;
  doctor)    do_doctor ;;
  ""|-h|--help) usage ;;
  *) err "Unknown action: $ACTION"; usage; exit 1 ;;
esac
