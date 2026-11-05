# RPiLEDConfig

*v0.4.0*

<img src="app/assets/icon.svg" alt="RPi LED Config icon" width="128">

Two-factor proximity setup for a Raspberry Pi. A daemon on the Pi offers a
Bluetooth LE service that answers only to someone who knows a one-time 64-bit
key, which it draws when a phone connects and blinks on the activity LED when
the phone asks. The phone app reads the LED with its camera, proves it knows
the key over BLE, and configures the Pi's WiFi over a channel encrypted with a
key derived from it. Line of sight and Bluetooth
range are the two factors; nothing is typed and nothing is printed on the
board.

| Folder | What it is |
| --- | --- |
| `daemon/` | The service, `rpiledconfig`. C++20, Boost.Asio, OpenSSL, BlueZ and NetworkManager over sd-bus; systemd. |
| `app/` | The phone app, RPi LED Config. React Native, TypeScript; react-native-ble-plx, react-native-vision-camera and the noble libraries, and nothing else at run time. The LED reader is native: `app/modules/led-reader`, C++ behind a VisionCamera frame-processor plugin. |
| `docs/` | The protocol (`protocol.md`), installation, configuration, releasing to Google Play, the app's privacy policy. |
| `packaging/` | The Docker build of the Debian package. The package's own files are in `daemon/packaging/`. |
| `TODO/` | `ACTIVE.md` is the plan; `COMPLETED.md` the record. |

## How it works

1. On a Pi with no WiFi connection the daemon enters setup mode: BLE
   advertises `RPiLEDConfig-<4 random hex>` (the prefix is configurable). The
   LED is left alone.
2. The app lists the Pis nearby by name; choosing one asks it, over
   BLE, to blink. The Pi draws a key for this pairing, and its activity LED
   blinks it (frame version 7:
   run-limited five-symbol words, Reed-Solomon coded with a CRC, at the speed
   chosen in the app: 33, 51 or 77 ms a symbol, 5.6, 8.7 or 13.1 seconds a
   frame) for up to
   two minutes.
3. The app's camera is pointed roughly at the Pi: the reader finds the
   blinking LED anywhere in the picture and decodes the key, in under ten
   seconds from first sight.
4. The app connects over BLE, and the two sides prove to each other that
   they hold the key with SPAKE2 (RFC 9382), so nothing on the air can be
   used to test guesses of it offline. A session key is derived and
   everything after is AES-GCM. The LED stops.
5. The app lists the networks the Pi can see; the user picks one and types
   its passphrase, or types a network's name by hand.
   The daemon hands the network to NetworkManager and reports what became
   of it, so a wrong passphrase or a name nothing broadcasts is shown the
   moment NetworkManager gives up. On success, Finish takes the Pi out of
   setup mode, and the key is gone.

Once paired, the app can also remove RPiLEDConfig from the Pi altogether:
the daemon starts a root unit the package ships, which purges the package
and keeps the WiFi it set up (docs/protocol.md, Uninstalling).

`docs/protocol.md` has every byte of this.

## Installing

On Raspberry Pi OS (bookworm or later, 64-bit):

    sudo apt install ./rpiledconfig_0.4.0-1_arm64.deb
    sudo systemctl start rpiledconfig

The daemon needs BlueZ and NetworkManager, both of which Raspberry Pi OS
ships. `docs/installation.md` covers the rest. The package is built natively
on a Pi: `cmake --build build-deb && (cd build-deb && cpack -G DEB)` in
`daemon/`.

## Building

The daemon builds on the Mac with fake LED, BLE and WiFi backends, which is
how it is developed and tested:

    cd daemon
    cmake --preset tests && cmake --build build-tests -j8 && ./build-tests/rpiledconfig_tests

The app (Node 22 or later, a JDK and the Android SDK with NDK 27 for the
phone build):

    cd app
    npm install && npm run typecheck && npm test

The native LED reader's C++ core and its tests:

    cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja
    cmake --build app/modules/led-reader/cpp/build && app/modules/led-reader/cpp/build/led_reader_tests

The Android app, with the reader built in:

    cd app/android && ./gradlew assembleRelease

`app/android` is checked in; `scripts/make-icons.sh` regenerates its icons
and the Play listing's graphics from `assets/icon.svg`. iOS has no project
yet. `docs/play-store.md` covers signing and releasing to Google Play.

## License

Apache License 2.0; see LICENSE and NOTICE.
