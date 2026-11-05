# RPiLEDConfig

*v0.4.0*

<img src="app/assets/icon.svg" alt="RPi LED Config icon" width="128">

Set up a Raspberry Pi's WiFi from a phone, with no keyboard, screen or network
on the Pi. A service on the Pi offers a Bluetooth LE service that answers only
to someone who knows a one-time 64-bit key, which it draws when a phone connects
and blinks on the activity LED when the phone asks. The phone app reads the LED
with its camera, proves it knows the key over Bluetooth, and sends the WiFi
details over a channel encrypted with a key derived from it. Sight of the LED
and Bluetooth range are the two factors. There is no pairing code to type and
nothing printed on the board; the key is kept in memory for one pairing, never
on disk.

| Folder | What it is |
| --- | --- |
| `daemon/` | The service, `rpiledconfig`. C++20, Boost.Asio, OpenSSL, BlueZ and NetworkManager over sd-bus; systemd. |
| `app/` | The phone app, RPi LED Config. React Native and TypeScript, Android only for now. At run time: react-native-ble-plx, react-native-vision-camera with react-native-worklets-core, react-native-safe-area-context and the noble libraries. The LED reader is native: `app/modules/led-reader`, C++ behind a VisionCamera frame-processor plugin. |
| `docs/` | The protocol (`protocol.md`), installation, configuration, releasing to Google Play (`play-store.md`), and the app's privacy policy (`privacy.md`). |
| `packaging/` | A Docker build of the Debian package, kept but not used. The package's own files are in `daemon/packaging/`. |
| `TODO/` | `ACTIVE.md` is the plan; `COMPLETED.md` the record. |

## Status

The latest tag, 0.4.0, does not contain the Android project and speaks an older
protocol, so build from `main`. There is no published package or app store
release yet. Everything so far has been run on one Raspberry Pi 5 on Raspberry
Pi OS trixie and one Blackview A85 on Android 12, in one dark room;
`TODO/COMPLETED.md` has the measurements and `TODO/ACTIVE.md` what is untested.

## How it works

1. On a Pi with no WiFi connection the service enters setup mode: it advertises
   over Bluetooth as `RPiLEDConfig-<4 random hex>`. The prefix is configurable
   (`ble.name_prefix`), and the name says nothing about the key. The LED is left
   alone. Setup mode ends on Finish or after 15 minutes, and comes back while
   there is still no WiFi.
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
   nothing broadcasts is shown when NetworkManager gives up. Finish takes the Pi
   out of setup mode, and the key is discarded; it also goes after two minutes
   with no phone connected and the LED off.

Once paired, the app can also remove RPiLEDConfig from the Pi altogether: the
service starts a root unit the package ships, which purges the package and keeps
the WiFi it set up (docs/protocol.md, Uninstalling).

A release of the app has no permission to use the internet (docs/privacy.md).

`docs/protocol.md` has every byte of this.

## Installing the service

There is no package to download yet. Build it natively on a Pi, on 64-bit
Raspberry Pi OS, from `daemon/`:

    cmake -S . -B build-deb -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DRPILEDCONFIG_BUILD_TESTING=OFF
    cmake --build build-deb && (cd build-deb && cpack -G DEB)
    sudo apt install ./build-deb/rpiledconfig_0.4.0-1_arm64.deb

The package enables and starts the service. It needs BlueZ, NetworkManager and
polkit, which Raspberry Pi OS ships. `docs/installation.md` covers the rest, and
`docs/configuration.md` every setting.

## Building

The service builds on the Mac with fake LED, BLE and WiFi backends, which is how
it is developed and tested. The tests that need D-Bus run only on Linux:

    cd daemon
    cmake --preset tests && cmake --build build-tests -j8 && ./build-tests/rpiledconfig_tests

The app (Node 22 or later, a JDK and the Android SDK with NDK 27 for the phone
build):

    cd app
    npm install && npm run typecheck && npm test

The native LED reader's C++ core and its tests:

    cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja
    cmake --build app/modules/led-reader/cpp/build && app/modules/led-reader/cpp/build/led_reader_tests

The Android app, with the reader built in:

    cd app/android && ./gradlew assembleRelease

Without an upload key that is signed with the debug key, which installs on a
test phone and which Google Play refuses. `docs/play-store.md` covers the key,
the bundle (`./gradlew bundleRelease`) and the listing. Do not run `./gradlew
clean`; that file says why.

`app/android` is checked in, and `scripts/make-icons.sh` regenerates its icons
and the Play listing's graphics from `assets/icon.svg`. iOS has no project yet.

## License

Apache License 2.0; see LICENSE and NOTICE.
