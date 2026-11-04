# RPiLEDConfig

*v0.3.0*

Two-factor proximity setup for a Raspberry Pi. A daemon on the Pi offers a
Bluetooth LE service that answers only to someone who knows its 64-bit
machine ID, and blinks that ID on the activity LED when a phone asks. The
phone app reads the LED with its camera, proves it knows the ID over BLE, and configures the Pi's WiFi over a
channel encrypted with a key derived from it. Line of sight and Bluetooth
range are the two factors; nothing is typed and nothing is printed on the
board.

| Folder | What it is |
| --- | --- |
| `daemon/` | The service, `rpiledconfig`. C++20, Boost.Asio, OpenSSL, BlueZ and NetworkManager over sd-bus; systemd. |
| `app/` | The phone app. Expo (React Native), TypeScript, react-native-ble-plx, react-native-vision-camera. The LED reader is native: `app/modules/led-reader`, C++ behind a VisionCamera frame-processor plugin. |
| `docs/` | The protocol (`protocol.md`), installation, configuration. |
| `packaging/` | The Docker build of the Debian package. The package's own files are in `daemon/packaging/`. |
| `TODO/` | `ACTIVE.md` is the plan; `COMPLETED.md` the record. |

## How it works

1. On a Pi with no WiFi connection the daemon enters setup mode: BLE
   advertises `RPiLEDConfig-<fingerprint>`. The LED is left alone.
2. The app lists the Pis nearby by fingerprint; choosing one asks it, over
   BLE, to blink. Its activity LED blinks the machine ID (frame version 6:
   run-limited five-symbol words at 50 ms a symbol, 16 bits per second,
   Reed-Solomon coded with a CRC, 8.5 seconds a frame) for up to two
   minutes.
3. The app's camera is pointed roughly at the Pi: the reader finds the
   blinking LED anywhere in the picture and decodes the ID, in under ten
   seconds from first sight, and checks it is the chosen Pi's.
4. The app connects over BLE, and the two sides prove to each other that
   they hold the ID with SPAKE2 (RFC 9382), so nothing on the air can be
   used to test guesses of it offline. A session key is derived and
   everything after is AES-GCM. The LED stops.
5. The app lists the networks the Pi can see and sends the chosen SSID and
   passphrase. The daemon hands them to NetworkManager, confirms the
   connection, and leaves setup mode.

Once paired, the app can also remove RPiLEDConfig from the Pi altogether:
the daemon starts a root unit the package ships, which purges the package
and keeps the WiFi it set up (docs/protocol.md, Uninstalling).

`docs/protocol.md` has every byte of this.

## Installing

On Raspberry Pi OS (bookworm or later, 64-bit):

    sudo apt install ./rpiledconfig_0.3.0-1_arm64.deb
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

The app:

    cd app
    npm install && npm run typecheck && npm test

The native LED reader's C++ core and its tests:

    cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja
    cmake --build app/modules/led-reader/cpp/build && app/modules/led-reader/cpp/build/led_reader_tests

The Android app, with the reader built in: `npx expo prebuild`, then
`./gradlew assembleRelease` in `app/android`. iOS does not have the reader
yet.

## License

Apache License 2.0; see LICENSE and NOTICE.
