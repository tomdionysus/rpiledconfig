# Building

The three parts build separately: the daemon (`daemon/`, CMake), the LED reader's C++ core
(`app/modules/led-reader/cpp`, CMake), and the Android app (`app/`, npm and Gradle), which
builds the LED reader into itself. `docs/packaging.md` covers the Debian package and
`docs/development.md` working on the code.

## The daemon

On Debian trixie or Raspberry Pi OS (trixie), with

    sudo apt install build-essential cmake ninja-build pkg-config libboost-dev libssl-dev libsystemd-dev libgtest-dev dbus-daemon

build and test:

    cmake -S daemon -B daemon/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRPILEDCONFIG_BUILD_TESTING=ON -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
    cmake --build daemon/build -j4
    dbus-run-session -- sh -c 'RPILEDCONFIG_TEST_DBUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS exec daemon/build/rpiledconfig_tests'

The D-Bus tests (the drivers against a mock BlueZ and NetworkManager) run only when
`RPILEDCONFIG_TEST_DBUS_ADDRESS` names a bus, and skip otherwise; `dbus-run-session` gives
them a private one, and `tools/linux-tests.sh` does the same with a bus of its own. The system
bus is never touched.

On a Mac, with MacPorts (`sudo port install cmake ninja openssl3 gtest` and a versioned Boost
port of 1.80 or later, such as `boost191`; the newest under `/opt/local/libexec/boost` is
used), the same commands build the daemon with its fake LED, Bluetooth and WiFi backends, and
`daemon/build/rpiledconfig_tests` runs every test but the D-Bus and wpa_supplicant ones, which
are not built there.

| Option | Default | What it does |
| --- | --- | --- |
| `RPILEDCONFIG_BUILD_TESTING` | `OFF` | Builds `rpiledconfig_tests` |
| `RPILEDCONFIG_WARNINGS_AS_ERRORS` | `OFF` | Treats the compiler's warnings as errors |
| `RPILEDCONFIG_BUILD_FUZZING` | `OFF` | Builds the fuzz targets (clang only) |
| `RPILEDCONFIG_SANITIZE` | `none` | `address` (with UndefinedBehaviorSanitizer) or `thread` |
| `RPILEDCONFIG_COVERAGE` | `OFF` | gcov instrumentation, for an lcov report |
| `RPILEDCONFIG_INSTALL_SYSTEM_FILES` | `ON` on Linux | Installs the systemd units, the udev rule and the polkit rule |

`daemon/CMakePresets.json` has presets for the usual combinations: `default` (Debug, into
`daemon/build`), and `tests`, `asan`, `tsan`, `coverage` and `release`, each into
`daemon/build-<preset>`. Without a preset or a build type, the build type is Release. Configuring checks that the daemon's version, `app/app.json` and the README's version
line agree (`daemon/packaging/check_version.cmake`).

## Fuzzing

With `apt install clang libclang-rt-19-dev`, the fuzz targets run what comes over Bluetooth
against arbitrary input:

    CXX=clang++ cmake -S daemon -B daemon/build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRPILEDCONFIG_BUILD_FUZZING=ON
    cmake --build daemon/build-fuzz --target fuzz_attacker fuzz_parsers
    daemon/build-fuzz/fuzz_attacker -max_total_time=900 -len_control=0 -max_len=4096

## The LED reader

The reader's C++ core and its tests, on Linux or a Mac:

    cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
    cmake --build app/modules/led-reader/cpp/build
    app/modules/led-reader/cpp/build/led_reader_tests

The tests include every recording in `app/src/led/fixtures` (docs/development.md).

## The app

With Node 22 or later, a JDK 17, and the Android SDK with NDK 27:

    cd app
    npm install && npm run typecheck && npm run lint && npm test

The Android app, with the LED reader built in:

    cd app/android && ./gradlew assembleRelease

Without an upload key that is signed with the debug key, which installs on a test phone and
which Google Play refuses. `docs/play-store.md` covers the key, the bundle
(`./gradlew bundleRelease`) and the listing. Do not run `./gradlew clean`; that file says why.

A build made with `-Prpiledconfig.diagnostics=true` logs what the LED reader measures, once a
second, and every camera frame's brightness of the tile it reads (docs/development.md).

`app/scripts/make-icons.sh` regenerates the app's icons, the README's logo and the Play
listing's graphics from `app/assets/icon.svg`.
