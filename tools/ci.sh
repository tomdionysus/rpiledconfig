#!/bin/sh
#
# RPiLEDConfig - tools
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# One CI step for the native code, the daemon and the app's LED reader, on Debian trixie, in
# GitHub's runner or on a build machine; .github/workflows/ci.yml runs each as its own job.
# The app's TypeScript is checked by npm in that workflow.
#
#   tools/ci.sh check      the header block on every source, and clang-format
#   tools/ci.sh release    g++ release builds, warnings as errors, and the tests
#   tools/ci.sh clang      the same with clang++ (and -Wthread-safety)
#   tools/ci.sh asan       AddressSanitizer and UndefinedBehaviorSanitizer
#   tools/ci.sh tsan       ThreadSanitizer
#   tools/ci.sh coverage   coverage builds' tests; lcov reports build-ci/daemon.info and
#                          build-ci/led-reader.info
#   tools/ci.sh fuzz       each fuzz target for FUZZ_SECONDS (default 60), with ASan and
#                          UBSan, under clang's libFuzzer
#
# Builds go under build-ci/ (CI_OUT to move it); CI_JOBS sets the parallel jobs (default:
# every core). Needs g++ cmake ninja-build pkg-config libboost-dev libssl-dev libsystemd-dev
# libgtest-dev dbus-daemon, and for its step clang, clang-format-19, libclang-rt-19-dev or
# lcov. The daemon's D-Bus tests get a private bus of their own; the system bus is never
# touched.

HEADER_PREFIX='RPiLEDConfig - '
COPYRIGHT='Copyright (C) 2026 Tom Cully <mail@tomcully.com>'

set -eu

top=$(cd "$(dirname "$0")/.." && pwd)
out=${CI_OUT:-$top/build-ci}
jobs=${CI_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}
step=${1:-}
reader=$top/app/modules/led-reader/cpp
generator=
if command -v ninja >/dev/null 2>&1; then generator="-G Ninja"; fi

# Warnings are errors in every build but the sanitizers' and coverage's: instrumented, g++
# warns inside the standard library. The release and clang steps hold the warning gate.
# Configures, and on failure prints CMake's own log of what it tried, which a CI runner's
# output would not otherwise show.
configure() {
  build="$1"
  shift
  # shellcheck disable=SC2086
  if ! cmake "$@" -B "$build" $generator; then
    cat "$build/CMakeFiles/CMakeConfigureLog.yaml" 2>/dev/null || true
    return 1
  fi
}

daemon() {
  dir="$1"
  shift
  configure "$out/daemon-$dir" -S "$top/daemon" -DRPILEDCONFIG_BUILD_TESTING=ON "$@"
  cmake --build "$out/daemon-$dir" -j "$jobs" --target rpiledconfig rpiledconfig_tests
}

led_reader() {
  dir="$1"
  shift
  configure "$out/led-reader-$dir" -S "$reader" "$@"
  cmake --build "$out/led-reader-$dir" -j "$jobs"
}

# The daemon's tests with a private bus.
daemon_tests() {
  dbus-run-session -- sh -c 'RPILEDCONFIG_TEST_DBUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" exec "$0"' "$1"
}

lcov_report() {
  dir=$1
  source=$2
  report=$3
  shift 3
  ignore=inconsistent,inconsistent,mismatch,negative,unused
  lcov --quiet --capture --directory "$dir" --ignore-errors "$ignore" --output-file "$report.all"
  lcov --quiet --extract "$report.all" "$source/*" --ignore-errors "$ignore" --output-file "$report.project"
  if [ $# -gt 0 ]; then
    lcov --quiet --remove "$report.project" "$@" --ignore-errors "$ignore" --output-file "$report"
  else
    mv "$report.project" "$report"
  fi
  rm -f "$report.all" "$report.project"
  lcov --summary "$report" --ignore-errors "$ignore"
}

# A source's header block: within its first four lines (a shebang may come first), a line
# naming the project and the part, and the copyright line.
has_header() {
  comment=$1
  file=$2
  head -5 "$file" | grep -q "^$comment $HEADER_PREFIX" && head -6 "$file" | grep -qF "$comment $COPYRIGHT"
}

case "$step" in
  check)
    cd "$top"
    missing=0
    for f in $(git ls-files '*.h' '*.cpp' '*.h.in' '*.ts' '*.tsx' '*.js' '*.mjs' '*.kt' '*.gradle' | grep -v '^app/android/app/src/main/res/'); do
      has_header '//' "$f" || { echo "no header block: $f"; missing=1; }
    done
    for f in $(git ls-files 'CMakeLists.txt' '*/CMakeLists.txt' '*.sh' '*.yml' '*.cmake'); do
      has_header '#' "$f" || { echo "no header block: $f"; missing=1; }
    done
    [ "$missing" = 0 ]
    clang_format=$(command -v clang-format-19 || command -v clang-format)
    git ls-files 'daemon/*.h' 'daemon/*.cpp' 'app/modules/led-reader/*.h' 'app/modules/led-reader/*.cpp' | xargs "$clang_format" --dry-run -Werror
    ;;
  release)
    daemon release -DCMAKE_BUILD_TYPE=Release -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
    daemon_tests "$out/daemon-release/rpiledconfig_tests"
    led_reader release -DCMAKE_BUILD_TYPE=Release -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
    "$out/led-reader-release/led_reader_tests"
    ;;
  clang)
    CXX=${CLANGXX:-clang++} daemon clang -DCMAKE_BUILD_TYPE=Release -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
    daemon_tests "$out/daemon-clang/rpiledconfig_tests"
    CXX=${CLANGXX:-clang++} led_reader clang -DCMAKE_BUILD_TYPE=Release -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
    "$out/led-reader-clang/led_reader_tests"
    ;;
  asan)
    daemon asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRPILEDCONFIG_SANITIZE=address
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 daemon_tests "$out/daemon-asan/rpiledconfig_tests"
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
    led_reader asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS="$flags"
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$out/led-reader-asan/led_reader_tests"
    ;;
  tsan)
    daemon tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRPILEDCONFIG_SANITIZE=thread
    TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" daemon_tests "$out/daemon-tsan/rpiledconfig_tests"
    flags="-fsanitize=thread -fno-omit-frame-pointer"
    led_reader tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS="$flags"
    TSAN_OPTIONS="halt_on_error=1" "$out/led-reader-tsan/led_reader_tests"
    ;;
  coverage)
    daemon coverage -DCMAKE_BUILD_TYPE=Debug -DRPILEDCONFIG_COVERAGE=ON
    find "$out/daemon-coverage" -name '*.gcda' -delete
    daemon_tests "$out/daemon-coverage/rpiledconfig_tests"
    lcov_report "$out/daemon-coverage" "$top/daemon/src" "$out/daemon.info"
    led_reader coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS=--coverage
    find "$out/led-reader-coverage" -name '*.gcda' -delete
    "$out/led-reader-coverage/led_reader_tests"
    lcov_report "$out/led-reader-coverage" "$reader" "$out/led-reader.info" '*/tests/*'
    ;;
  fuzz)
    CXX=${CLANGXX:-clang++} configure "$out/fuzz" -S "$top/daemon" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRPILEDCONFIG_BUILD_FUZZING=ON
    cmake --build "$out/fuzz" -j "$jobs" --target fuzz_attacker fuzz_parsers
    for target in fuzz_attacker fuzz_parsers; do
      corpus="$out/fuzz/corpus/$target"
      mkdir -p "$corpus"
      "$out/fuzz/$target" -max_total_time="${FUZZ_SECONDS:-60}" -len_control=0 -max_len=4096 -print_final_stats=1 "$corpus"
    done
    ;;
  *)
    echo "usage: tools/ci.sh check|release|clang|asan|tsan|coverage|fuzz" >&2
    exit 2
    ;;
esac
