#!/bin/sh
#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 T. Cully
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# The whole test suite on a Linux host (a Debian-family machine or a Pi), the D-Bus tests
# included: a private dbus-daemon is started for them, as this user, on a socket in a
# temporary directory, and stopped afterwards. The system bus is never touched; the tests
# own org.bluez and org.freedesktop.NetworkManager on the private bus only.
#
#   daemon/scripts/linux-tests.sh [--deps-env FILE] [gtest arguments...]
#
# Nothing is installed. What the build needs, from Debian packages:
#
#   build-essential cmake ninja-build pkg-config libboost-dev libssl-dev libsystemd-dev
#   libgtest-dev, and dbus-daemon (trixie; part of the "dbus" package on bookworm)
#
# or a prefix with the same unpacked into it: --deps-env FILE (or RPILEDCONFIG_DEPS_ENV)
# names a shell file that sets PATH, CMAKE_PREFIX_PATH and PKG_CONFIG_PATH for it, and is
# sourced first.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
daemon_dir=$(cd "$here/.." && pwd)

deps_env=${RPILEDCONFIG_DEPS_ENV:-}
if [ "${1:-}" = "--deps-env" ]; then
    [ $# -ge 2 ] || { echo "linux-tests.sh: --deps-env needs a file" >&2; exit 2; }
    deps_env=$2
    shift 2
fi

if [ -n "$deps_env" ]; then
    [ -r "$deps_env" ] || { echo "linux-tests.sh: cannot read $deps_env" >&2; exit 2; }
    # shellcheck disable=SC1090
    . "$deps_env"
fi

# --- What is needed, named rather than installed ------------------------------------------

missing=""
for tool in cmake ninja pkg-config c++ dbus-daemon; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists libsystemd || missing="$missing libsystemd(pkg-config)"
    pkg-config --exists openssl || missing="$missing openssl(pkg-config)"
fi

if [ -n "$missing" ]; then
    echo "linux-tests.sh: missing:$missing" >&2
    echo "  sudo apt install build-essential cmake ninja-build pkg-config libboost-dev libssl-dev libsystemd-dev libgtest-dev dbus-daemon" >&2
    echo "  (on bookworm, dbus-daemon is in the \"dbus\" package)" >&2
    echo "or name a dependency prefix with --deps-env FILE." >&2
    exit 1
fi

# --- The private bus ---------------------------------------------------------------------

work=$(mktemp -d "${TMPDIR:-/tmp}/rpiledconfig-tests.XXXXXX")
bus_pid=""

cleanup() {
    if [ -n "$bus_pid" ]; then
        kill "$bus_pid" 2>/dev/null || true
        wait "$bus_pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# A session-type bus for this user alone: anyone connected may own any name and call
# anything, which is what the mock BlueZ and NetworkManager need and what the system bus
# would never allow.
cat > "$work/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$work/bus</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF

dbus-daemon --config-file="$work/bus.conf" --nofork --nopidfile &
bus_pid=$!

tries=0
while [ ! -S "$work/bus" ]; do
    tries=$((tries + 1))
    if [ $tries -gt 100 ]; then
        echo "linux-tests.sh: the private dbus-daemon did not start" >&2
        exit 1
    fi
    sleep 0.05
done

RPILEDCONFIG_TEST_DBUS_ADDRESS="unix:path=$work/bus"
export RPILEDCONFIG_TEST_DBUS_ADDRESS
echo "Private bus: $RPILEDCONFIG_TEST_DBUS_ADDRESS (dbus-daemon $bus_pid)"

# --- Build and run -----------------------------------------------------------------------

cd "$daemon_dir"
cmake --preset tests
cmake --build build-tests -j"$(nproc 2>/dev/null || echo 2)"
./build-tests/rpiledconfig_tests "$@"
