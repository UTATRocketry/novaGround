#!/usr/bin/env bash
# ============================================================================
#  install.sh - one-time Raspberry Pi bootstrap for novaGround.
#
#  Installs the toolchain and libraries, then hands over to ./nova-pi.sh to
#  build and register the systemd service.
#
#  Run once on a fresh Pi:   sudo ./install.sh
#  Day-to-day operation afterwards is ./nova-pi.sh (start/stop/status/update).
#
#  It only asks which Pi this is (written to /etc/nova/pi.env). Each
#  environment's broker and backend come from the ops PC: Nova.ps1 writes
#  /etc/nova/novaGround-prod.env and -dev.env. The unit file itself is never
#  edited: an earlier version of this script used `sed -i` on the tracked
#  unit, which permanently dirtied the repo and made `git pull --ff-only` fail
#  on every subsequent update.
# ============================================================================
set -uo pipefail

if [ "$(id -u)" != "0" ]; then
   echo "This script must be run as root, i.e 'sudo ./install.sh'" 1>&2
   exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The user who will own and run novaGround. SUDO_USER is the account that
# invoked sudo, which is what we want -- not root.
RUN_USER="${SUDO_USER:-admin}"
BUILD_ROOT="$(cd "${HERE}/.." && pwd)"   # /home/admin/prod or /home/admin/dev

echo "novaGround installer"
echo "  repo:      ${HERE}"
echo "  run as:    ${RUN_USER}"
echo

# ── system packages ──────────────────────────────────────────────────────────

read -p "Would you like to update the system first? (y/n): " update_sys
if [ "$update_sys" == "y" ]; then
    echo "Updating system..."
    apt update && apt upgrade -y
else
    echo "Skipping system updates..."
fi

read -p "Install build tools and libraries? (y/n): " install_deps
if [ "$install_deps" == "y" ]; then
    echo "Installing build tools..."
    apt install -y build-essential clang meson ninja-build python3 \
                   libbz2-dev libz-dev libicu-dev git

    echo "Installing libraries..."
    apt install -y libpaho-mqtt-dev libgpiod-dev libcurl4-openssl-dev

    echo "Installing utilities..."
    # avahi-daemon publishes this Pi as <hostname>.local, so the ops PC can
    # reach it by name if its address ever changes.
    apt install -y tmux nmap avahi-daemon

    # -- MCC DAQ HAT library --
    if [ ! -d "${BUILD_ROOT}/daqhats" ]; then
        echo "Installing MCC DAQ HAT library..."
        sudo -u "$RUN_USER" git clone https://github.com/mccdaq/daqhats.git "${BUILD_ROOT}/daqhats"
        ( cd "${BUILD_ROOT}/daqhats" && ./install.sh )
    else
        echo "daqhats already present at ${BUILD_ROOT}/daqhats - skipping."
    fi

    # -- Boost --
    if [ ! -f /usr/local/include/boost/version.hpp ]; then
        echo "Installing boost (this takes a while)..."
        cd "$BUILD_ROOT"
        sudo -u "$RUN_USER" wget -q https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.bz2
        sudo -u "$RUN_USER" tar xf boost_1_81_0.tar.bz2
        cd boost_1_81_0
        ./bootstrap.sh --prefix=/usr/local
        ./b2
        ./b2 install

        BASHRC="/home/${RUN_USER}/.bashrc"
        # Only append if absent, so re-running does not stack duplicates.
        grep -q 'BOOST_ROOT' "$BASHRC" || {
            echo 'export BOOST_ROOT=/usr/local' >> "$BASHRC"
            echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> "$BASHRC"
            echo 'export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH' >> "$BASHRC"
        }
    else
        echo "boost already installed - skipping."
    fi

    # -- WiringPi (servo drivers) --
    if ! command -v gpio >/dev/null 2>&1; then
        echo "Installing WiringPi..."
        cd "$BUILD_ROOT"
        sudo -u "$RUN_USER" git clone https://github.com/WiringPi/WiringPi.git
        cd WiringPi
        sudo -u "$RUN_USER" ./build debian
        mv debian-template/wiringpi_3.18_arm64.deb .
        apt install -y ./wiringpi_3.18_arm64.deb
    else
        echo "WiringPi already installed - skipping."
    fi
fi

# ── which Pi this is ─────────────────────────────────────────────────────────
#
# Only the Pi's own identity is asked for here. The broker and backend
# addresses for each environment come from the ops PC: Nova.ps1 writes
# /etc/nova/novaGround-prod.env and -dev.env from its settings on every
# 'use', 'deploy' and 'get-changes', so they always match the stack that's live.

echo
read -p "Which Pi is this (Ground/Thermo/Mock) [Ground]: " build_type
case "${build_type:-Ground}" in
    Ground|ground|novaGround) target="novaGround" ;;
    Thermo|thermo|novaThermo) target="novaThermo" ;;
    Mock|mock|novaMock)       target="novaMock"   ;;
    *) echo "Unrecognised build '${build_type}', defaulting to novaGround"; target="novaGround" ;;
esac

# ── build and service registration ───────────────────────────────────────────

chmod +x "${HERE}/nova-pi.sh" "${HERE}/start.sh" "${HERE}/run.sh" 2>/dev/null

# Build the environment this checkout belongs to.
build_env="prod"
[ "$(basename "$BUILD_ROOT")" == "dev" ] && build_env="dev"

echo
read -p "Build novaGround ($build_env) now? (y/n): " do_build
if [ "$do_build" == "y" ]; then
    sudo -u "$RUN_USER" "${HERE}/nova-pi.sh" build --env "$build_env"
fi

echo
read -p "Install the novaGround@prod / novaGround@dev services (prod starts on boot)? (y/n): " install_service
if [ "$install_service" == "y" ]; then
    "${HERE}/nova-pi.sh" install --target "$target"
    echo
    echo "Then, on the ops PC:  .\\ops\\Nova.ps1 use prod"
    echo "That writes this Pi's prod and dev settings and starts novaGround@prod."
else
    echo "Skipping service installation."
    echo "Run manually with:  ${HERE}/run.sh"
fi

echo
echo "Done. Check the system with:  ${HERE}/nova-pi.sh doctor"
