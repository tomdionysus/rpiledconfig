<picture>
  <source media="(prefers-color-scheme: dark)" srcset="app/assets/logo-on-dark.svg">
  <img src="app/assets/logo-on-light.svg" alt="RPi LED Config logo" width="128">
</picture>

# RPiLEDConfig

*v0.5.1*

| What it shows | Status |
| --- | --- |
| CI: the service's tests, plain and under the sanitizers; the app's checks and tests; the Debian package; the Android build | [![CI](https://github.com/tomdionysus/rpiledconfig/actions/workflows/ci.yml/badge.svg)](https://github.com/tomdionysus/rpiledconfig/actions/workflows/ci.yml) |
| Test coverage, all three parts together | [![codecov](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg)](https://codecov.io/gh/tomdionysus/rpiledconfig) |
| Test coverage of the service | [![daemon coverage](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg?flag=daemon)](https://codecov.io/gh/tomdionysus/rpiledconfig?flags%5B0%5D=daemon) |
| Test coverage of the app's TypeScript | [![app coverage](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg?flag=app)](https://codecov.io/gh/tomdionysus/rpiledconfig?flags%5B0%5D=app) |
| Test coverage of the app's native LED reader | [![LED reader coverage](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg?flag=led-reader)](https://codecov.io/gh/tomdionysus/rpiledconfig?flags%5B0%5D=led-reader) |
| CodeQL security analysis of the C++ and the TypeScript | [![CodeQL](https://github.com/tomdionysus/rpiledconfig/actions/workflows/codeql.yml/badge.svg)](https://github.com/tomdionysus/rpiledconfig/actions/workflows/codeql.yml) |

WiFi on a Pi can be provisioned via a Bluetooth app, authenticated by a 64-bit secure one-time
key, communicated by flashing the Pi's activity LED and captured by the phone camera.

## What it's for

Provisioning Pis in the field, and changing their WiFi once they are there: a Pi
that has been installed somewhere without a screen or keyboard, where the network
was not known when its card was made, has since changed, or is one the Pi has
never seen.

If you know the network when you flash the card, set it there: Raspberry Pi
Imager does that, and this project is not trying to replace it. It is for what
happens after flashing, when the card is already in a Pi that is already in the
field.

The service has to be on the Pi beforehand, so it goes into the image the Pi is
flashed with. It advertises over Bluetooth for as long as it runs, whether the Pi
has WiFi or not, so a Pi's network can be changed while it is still connected.
`docs/security.md` says what that exposes. Once a Pi is settled, the app can remove
the service from it, `service.stop_after_boot_minutes` stops it a set time after each
boot, or `setup.always: false` makes it advertise only while the Pi has no WiFi
connection.

| Folder | What it is |
| --- | --- |
| `daemon/` | The service, `rpiledconfig`. C++20, Boost.Asio, OpenSSL, BlueZ and NetworkManager over sd-bus; systemd. |
| `app/` | The phone app, RPi LED Config. React Native and TypeScript, for Android. At run time: react-native-ble-plx, react-native-vision-camera with react-native-worklets-core, react-native-safe-area-context and the noble libraries. The LED reader is native: `app/modules/led-reader`, C++ behind a VisionCamera frame-processor plugin. |
| `docs/` | The protocol (`protocol.md`), what is exposed (`security.md`), installation, configuration, releasing to Google Play (`play-store.md`), and the app's privacy policy (`privacy.md`). |
| `debian/` | The Debian package, built with debhelper by `daemon/scripts/build-deb.sh`. The files it installs are in `daemon/packaging/`. |

## How it works

1. The service starts with the Pi and advertises over Bluetooth, whether the Pi has
   WiFi or not, as `RPiLEDConfig-<4 random hex>`; the prefix is configurable
   (`ble.name_prefix`), and the name says nothing about the key. The LED is left
   alone.
2. At first launch the app explains the permissions it needs and asks for them.
   It then lists the Pis it can hear now by their full names. Choosing one asks
   it, over Bluetooth, to blink at the speed chosen in the app, which the app
   remembers. The Pi has drawn a key on that first connection, and blinks it
   for up to two minutes.

   | Speed | Time per symbol | Time per frame |
   | --- | --- | --- |
   | Fast | 33 ms | 5.6 s |
   | Normal | 51 ms | 8.7 s |
   | Robust | 77 ms | 13.1 s |

   This is frame version 7: run-limited five-symbol words, Reed-Solomon coded
   with a CRC.
3. The camera is pointed roughly at the Pi. The reader looks for the blinking
   LED anywhere in the picture and decodes it. Only a frame that passes the CRC
   counts as a read, and the code's spare bytes mean that can happen before a
   whole frame has been watched. After three frames with no read the app says
   so and offers a slower speed.
4. The app connects again, and the two sides prove to each other that they hold
   the key with SPAKE2 (RFC 9382), so a recording of the Bluetooth traffic gives
   nothing to test guesses against offline. A key from another Pi's LED is
   refused here. A session key is derived and everything after is AES-GCM. The
   LED stops.
5. The app lists the networks the Pi can see. The user picks one, or types a
   name, and gives its passphrase. The service hands the network to
   NetworkManager and reports what became of it, so a wrong passphrase or a name
   nothing broadcasts is shown when NetworkManager gives up. Finish ends the
   pairing and discards the key; it also goes after two minutes
   with no phone connected and the LED off, or 15 minutes after it was drawn.

Once paired, the app can also remove RPiLEDConfig from the Pi altogether: the
service starts a root unit the package ships, which purges the package and keeps
the WiFi it set up (docs/protocol.md, Uninstalling).

A release of the app has no permission to use the internet (docs/privacy.md).

`docs/protocol.md` has every byte of this.

## Status

The protocol, the service and the Android app are complete and work
end to end: a Pi found, its LED read, SPAKE2, a network joined, and the service removed
from the phone.

| | Where it has been run |
| --- | --- |
| The service | Raspberry Pi 5 on 64-bit Raspberry Pi OS (trixie), with BlueZ and NetworkManager |
| The app | One Android 12 phone, in a dark room, at all three speeds |
| The package | Install, upgrade, removal, purge, and removal from the app, on that Pi |

CI runs the service's tests on Debian trixie, plain, under AddressSanitizer and
UndefinedBehaviorSanitizer, and for coverage; the service's Bluetooth parsers have fuzz
targets. Nothing here has had an outside security review.

## Roadmap

- Google Play: what is left is in `docs/play-store.md`.
- Pairing in a lit room, and on more phones.
- Wi-Fi Easy Connect from the app: the service's side is there and tested.
- An iOS app.

## Installing the service

Build the package on a Pi running 64-bit Raspberry Pi OS (trixie), from a clone of
this repository:

    sudo apt install dpkg-dev && sudo apt build-dep ./
    daemon/scripts/build-deb.sh
    sudo apt install ./build-deb/rpiledconfig_*_arm64.deb

The build runs the daemon's tests; `DEB_BUILD_OPTIONS=nocheck` skips them. The version
comes from `git describe`: a tag gives its own, such as `0.5.0`, and a build three
commits later `0.5.0+git3.gabc1234`. `rpiledconfig-dbgsym` beside it holds the debug
symbols.

The package enables and starts the service. It needs BlueZ, NetworkManager and
polkit, which Raspberry Pi OS ships. `docs/installation.md` covers the rest, and
`docs/configuration.md` every setting.

## Building

The service builds on the Mac with fake LED, BLE and WiFi backends, which is how
it is developed and tested. The tests that need D-Bus run only on Linux:

    cd daemon
    cmake --preset tests && cmake --build build-tests -j8 && ./build-tests/rpiledconfig_tests

On a Pi, with `apt install clang libclang-rt-19-dev`, the fuzzers run what comes over Bluetooth
against arbitrary input:

    cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DRPILEDCONFIG_FUZZ=ON
    cmake --build build-fuzz --target fuzz_attacker fuzz_parsers
    build-fuzz/fuzz_attacker -max_total_time=900 -len_control=0 -max_len=4096

On Debian trixie, or a Pi running it, `scripts/ci.sh` runs what CI runs for the daemon
and the LED reader: the format check, the tests plain and under the sanitizers, and the
coverage reports. `npm run coverage` in `app/` does the same for the app.

The app (Node 22 or later, a JDK and the Android SDK with NDK 27 for the phone
build):

    cd app
    npm install && npm run typecheck && npm run lint && npm test

The native LED reader's C++ core and its tests:

    cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja
    cmake --build app/modules/led-reader/cpp/build && app/modules/led-reader/cpp/build/led_reader_tests

The Android app, with the reader built in:

    cd app/android && ./gradlew assembleRelease

Without an upload key that is signed with the debug key, which installs on a
test phone and which Google Play refuses. `docs/play-store.md` covers the key,
the bundle (`./gradlew bundleRelease`) and the listing. Do not run `./gradlew
clean`; that file says why.

`scripts/make-icons.sh` regenerates the app's icons, the README's logo and the Play
listing's graphics from `assets/icon.svg`.

## Contributing

`CONTRIBUTING.md` has the rules for changes. A vulnerability is reported privately, as
`SECURITY.md` says, not in an issue.

## License

Apache License 2.0; see LICENSE and NOTICE.
