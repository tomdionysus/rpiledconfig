# Development

`CONTRIBUTING.md` has the rules; `docs/building.md` the build commands. This is how the code is
worked on and checked.

## The daemon

The daemon builds on a Mac with fake LED, Bluetooth and WiFi backends, which is how most of it
is developed and tested. What needs Linux (the BlueZ, NetworkManager and systemd drivers over
sd-bus, and Wi-Fi Easy Connect through wpa_supplicant) is tested on Linux against mocks on a
private bus, and then on a Pi with a phone.

Its tests are GoogleTest, one binary, `rpiledconfig_tests`, under `daemon/tests` mirroring
`daemon/src`, written from `docs/protocol.md` rather than from what the code does. Run the
binary, not ctest. `rpiledconfig --print-vectors` prints the protocol's test vectors, which
`app/src/protocol/vectors.ts` must match.

## The app and the LED reader

The app's logic is tested with Vitest (`npm test`, `npm run coverage`); components are not
rendered in tests, and the native modules are stubbed in `app/src/test/`. The LED reader's C++
core has its own GoogleTest binary, `led_reader_tests`, with a synthesiser of what a phone's
camera sees (`app/modules/led-reader/cpp/tests/synth.h`).

Every recording in `app/src/led/fixtures` is also a test of the reader: a frame version 7
recording (`-v7` in its name) must read the ID in its `expected`, within `expected.withinMs`
if given, at its `symbolMs`; any other must read nothing. A recording is the brightness, frame
by frame, of the tile the reader chose, with each frame's time. A diagnostics build of the app
(`./gradlew assembleRelease -Prpiledconfig.diagnostics=true`) logs exactly that, a second at a
time, as `[samples]` lines, with `[reader]` lines saying what the reader measured:

    adb logcat -s ReactNativeJS

## CI

`tools/ci.sh <step>` runs one step of the native CI on Debian trixie, the same in GitHub's
runner and on any trixie machine:

| Step | What it runs |
| --- | --- |
| `check` | The header block on every source, and clang-format 19 over the C++ |
| `release` | g++ release builds of the daemon and the LED reader, warnings as errors, and their tests |
| `clang` | The same with clang++ and `-Wthread-safety` |
| `asan` | Both under AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | Both under ThreadSanitizer |
| `coverage` | Coverage builds' tests, with lcov reports `build-ci/daemon.info` and `build-ci/led-reader.info` |
| `fuzz` | Each fuzz target for `FUZZ_SECONDS` (default 60) |

`.github/workflows/ci.yml` runs each step as its own job, on x86-64 and, for `release` and
`asan`, on arm64 as a Pi builds it; then the Debian package on arm64 with lintian, a build and
test on macOS, the Android release build, and the app's typecheck, lint, format check, version
check and tests with coverage. Coverage goes to Codecov under the flags `daemon`, `app` and
`led-reader`. CodeQL analyses the C++ and the TypeScript.

CI has no Bluetooth, LED, WiFi or phone. A change to any of those is also run on a Pi with a
phone: the package installed, a phone finding the Pi, reading its LED at each speed, and
authenticating.
