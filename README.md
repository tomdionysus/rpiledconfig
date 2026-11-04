# RPiLEDConfig

*v0.1.0 (in development)*

Two-factor proximity setup for a Raspberry Pi. A daemon on the Pi blinks a
64-bit machine ID on the activity LED and offers a Bluetooth LE service that
answers only to someone who knows it. The phone app reads the LED with its
camera, proves it knows the ID over BLE, and configures the Pi's WiFi over a
channel encrypted with a key derived from it. Line of sight and Bluetooth
range are the two factors; nothing is typed and nothing is printed on the
board.

| Folder | What it is |
| --- | --- |
| `daemon/` | The service, `rpiledconfig`. C++20, Boost.Asio, OpenSSL, BlueZ and NetworkManager over sd-bus; systemd. |
| `app/` | The phone app. Expo (React Native), TypeScript, react-native-ble-plx, react-native-vision-camera. |
| `docs/` | The protocol (`protocol.md`), installation, configuration. |
| `packaging/` | The Docker build of the Debian package. The package's own files are in `daemon/packaging/`. |
| `TODO/` | `ACTIVE.md` is the plan; `COMPLETED.md` the record. |

## How it works

1. On a Pi with no WiFi connection the daemon enters setup mode: its
   activity LED blinks the machine ID (Manchester, 10 bits per second,
   Reed-Solomon coded with a CRC)
   and BLE advertises `RPiLEDConfig-<fingerprint>`.
2. The app's camera is pointed at the LED for about 20 seconds (one 19.9-second frame) and decodes
   the ID.
3. The app connects over BLE, and the two sides prove to each other that
   they hold the ID with SPAKE2 (RFC 9382), so nothing on the air can be
   used to test guesses of it offline. A session key is derived and
   everything after is AES-GCM.
4. The app lists the networks the Pi can see and sends the chosen SSID and
   passphrase. The daemon hands them to NetworkManager, confirms the
   connection, and leaves setup mode.

`docs/protocol.md` has every byte of this.

## Installing

On Raspberry Pi OS (bookworm or later, 64-bit):

    sudo apt install ./rpiledconfig_0.1.0-1~deb12_arm64.deb
    sudo systemctl start rpiledconfig

The daemon needs BlueZ and NetworkManager, both of which Raspberry Pi OS
ships. `docs/installation.md` covers the rest. `packaging/build-debs.sh`
builds the packages in Docker into `dist/`.

## Building

The daemon builds on the Mac with fake LED, BLE and WiFi backends, which is
how it is developed and tested:

    cd daemon
    cmake --preset tests && cmake --build build-tests -j8 && ./build-tests/rpiledconfig_tests

The app:

    cd app
    npm install && npm run typecheck && npm test

## License

Apache License 2.0; see LICENSE and NOTICE.
