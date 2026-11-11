#!/bin/sh
#
# RPiLEDConfig
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# The native half of CI: the daemon's format check, its tests in a plain build and under
# AddressSanitizer and UndefinedBehaviorSanitizer, a coverage build of each of the daemon and
# the LED reader with an lcov report, and the LED reader's tests. Runs on Debian trixie, in
# GitHub's runner or on a Pi. The app's half is in .github/workflows/ci.yml.
#
#   scripts/ci.sh
#
# Needs: build-essential cmake ninja-build pkg-config libboost-dev libssl-dev libsystemd-dev
# libgtest-dev dbus-daemon lcov clang-format-19. The D-Bus tests get a private bus of their
# own; the system bus is never touched. The reports are build-coverage/daemon.info and
# build-coverage/led-reader.info.

set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"
jobs=$(nproc 2>/dev/null || echo 4)
reports=$top/build-coverage
mkdir -p "$reports"

step() { printf '\n== %s\n' "$1"; }

# The daemon's tests with a private bus.
daemon_tests() {
    dbus-run-session -- sh -c 'RPILEDCONFIG_TEST_DBUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" exec "$0"' "$1"
}

lcov_report() {
    dir=$1
    source=$2
    out=$3
    shift 3
    lcov --quiet --capture --directory "$dir" --ignore-errors mismatch,inconsistent,inconsistent,negative --output-file "$out.all"
    lcov --quiet --extract "$out.all" "$source/*" --ignore-errors inconsistent,inconsistent --output-file "$out.project"
    if [ $# -gt 0 ]; then
        lcov --quiet --remove "$out.project" "$@" --ignore-errors inconsistent,inconsistent --output-file "$out"
    else
        mv "$out.project" "$out"
    fi
    rm -f "$out.all" "$out.project"
    lcov --summary "$out" --ignore-errors inconsistent,inconsistent
}

step "format"
clang_format=$(command -v clang-format-19 || command -v clang-format)
find daemon/src daemon/tests \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 "$clang_format" --dry-run -Werror

step "daemon: tests"
(cd daemon && cmake --preset tests >/dev/null)
cmake --build daemon/build-tests -j "$jobs"
daemon_tests daemon/build-tests/rpiledconfig_tests

step "daemon: ASan and UBSan"
(cd daemon && cmake --preset asan >/dev/null)
cmake --build daemon/build-asan -j "$jobs"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 daemon_tests daemon/build-asan/rpiledconfig_tests

step "daemon: coverage"
(cd daemon && cmake --preset coverage >/dev/null)
find daemon/build-coverage -name '*.gcda' -delete
cmake --build daemon/build-coverage -j "$jobs"
daemon_tests daemon/build-coverage/rpiledconfig_tests
lcov_report daemon/build-coverage "$top/daemon/src" "$reports/daemon.info"

step "LED reader: tests and coverage"
reader=app/modules/led-reader/cpp
cmake -S "$reader" -B "$reader/build-coverage" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS=--coverage >/dev/null
find "$reader/build-coverage" -name '*.gcda' -delete
cmake --build "$reader/build-coverage" -j "$jobs"
"$reader/build-coverage/led_reader_tests"
lcov_report "$reader/build-coverage" "$top/$reader" "$reports/led-reader.info" '*/tests/*'

step "passed"
