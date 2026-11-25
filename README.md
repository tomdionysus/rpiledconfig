<picture>
  <source media="(prefers-color-scheme: dark)" srcset="app/assets/logo-on-dark.svg">
  <img src="app/assets/logo-on-light.svg" alt="RPi LED Config logo" width="128">
</picture>

# RPiLEDConfig

*v0.5.2*

| What it shows | Status |
| --- | --- |
| The latest release: the Debian package, with its debug symbols | [![Release](https://img.shields.io/github/v/release/tomdionysus/rpiledconfig?display_name=release&label=latest)](https://github.com/tomdionysus/rpiledconfig/releases/latest) |
| CI: the daemon and the LED reader built and tested with g++, clang and the sanitizers; the Debian package; macOS; the Android build; the app's checks and tests | [![CI](https://github.com/tomdionysus/rpiledconfig/actions/workflows/ci.yml/badge.svg)](https://github.com/tomdionysus/rpiledconfig/actions/workflows/ci.yml) |
| Test coverage, all three parts together | [![codecov](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg)](https://codecov.io/gh/tomdionysus/rpiledconfig) |
| Test coverage of the service | [![daemon coverage](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg?flag=daemon)](https://codecov.io/gh/tomdionysus/rpiledconfig?flags%5B0%5D=daemon) |
| Test coverage of the app's TypeScript | [![app coverage](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg?flag=app)](https://codecov.io/gh/tomdionysus/rpiledconfig?flags%5B0%5D=app) |
| Test coverage of the app's native LED reader | [![LED reader coverage](https://codecov.io/gh/tomdionysus/rpiledconfig/graph/badge.svg?flag=led-reader)](https://codecov.io/gh/tomdionysus/rpiledconfig?flags%5B0%5D=led-reader) |
| CodeQL security analysis of the C++ and the TypeScript | [![CodeQL](https://github.com/tomdionysus/rpiledconfig/actions/workflows/codeql.yml/badge.svg)](https://github.com/tomdionysus/rpiledconfig/actions/workflows/codeql.yml) |
| The licence | [![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE) |

WiFi on a Pi can be provisioned via a Bluetooth app, authenticated by a 64-bit secure one-time
key, communicated by flashing the Pi's activity LED and captured by the phone camera.

## What it's for

Provisioning Pis in the field, and changing their WiFi once they are there: a Pi that has
been installed somewhere without a screen or keyboard, where the network was not known when
its card was made, has since changed, or is one the Pi has never seen.

If you know the network when you flash the card, set it there: Raspberry Pi Imager does that,
and this project is not trying to replace it. It is for what happens after flashing, when the
card is already in a Pi that is already in the field.

The service has to be on the Pi beforehand, so it goes into the image the Pi is flashed with.
It advertises over Bluetooth for as long as it runs, whether the Pi has WiFi or not, so a Pi's
network can be changed while it is still connected. `docs/security.md` says what that
exposes. Once a Pi is settled, the app can remove the service from it,
`service.stop_after_boot_minutes` stops it a set time after each boot, or `setup.always:
false` makes it advertise only while the Pi has no WiFi connection.

## How it works

1. The service starts with the Pi and advertises over Bluetooth as `RPiLEDConfig-<4 random
   hex>`. The LED is left alone.
2. The app lists the Pis it can hear. Choosing one asks it, over Bluetooth, to blink a
   one-time key it draws then, at the speed chosen in the app: Fast, Normal or Robust.
3. The camera is pointed roughly at the Pi. The app finds the blinking LED anywhere in the
   picture and reads the key; only a frame that passes its CRC counts as a read. The LED may
   be wired either way up: the reader decodes both.
4. The app and the Pi prove to each other that they hold the key with SPAKE2 (RFC 9382), so a
   recording of the Bluetooth traffic gives nothing to test guesses against offline. Everything
   after is AES-GCM, and the LED stops.
5. The app lists the networks the Pi can see; the user picks one and gives its passphrase. The
   service hands it to NetworkManager and reports what became of it. Finish discards the key.

Once paired, the app can also remove RPiLEDConfig from the Pi altogether, keeping the WiFi it
set up. A release of the app has no permission to use the internet (`docs/privacy.md`).
`docs/protocol.md` has every byte of this.

## Installing the service

On a Raspberry Pi running 64-bit Raspberry Pi OS (trixie), install the package from the
[latest release](https://github.com/tomdionysus/rpiledconfig/releases/latest):

    sudo apt install ./rpiledconfig_<version>_arm64.deb

It needs BlueZ, NetworkManager and polkit, which Raspberry Pi OS ships, and enables and starts
the service. To build the package yourself, see `docs/packaging.md`.

## Setting up a Pi

1. Install the app, RPi LED Config, on an Android phone.
2. In the app, choose Find the Pi, then the Pi by its name.
3. Point the camera at the Pi's activity LED and hold still until it reads.
4. Pick the network and give its passphrase.

## Checking it works

    systemctl status rpiledconfig
    sudo journalctl -u rpiledconfig -f

The journal says `Advertising RPiLEDConfig-<4 hex>` once the service is up, and the app lists
the Pi under that name. When the app asks, it says `Blinking the key`, then `Authenticated`
once the phone has read it. `docs/installation.md` has the rest.

## Documentation

| Document | What it covers |
| --- | --- |
| `docs/installation.md` | What the package installs, first run, permissions, upgrading, removing |
| `docs/configuration.md` | Every setting |
| `docs/protocol.md` | The LED channel, the Bluetooth protocol, and the test vectors |
| `docs/security.md` | What the service exposes |
| `docs/privacy.md` | The app's privacy policy |
| `docs/building.md` | Building the daemon, the LED reader and the app |
| `docs/packaging.md` | Building the Debian package, and its version |
| `docs/development.md` | Tests, recordings, diagnostics and CI |
| `docs/play-store.md` | Releasing the app to Google Play |
| `docs/licensing.md` | The licences of what this is built from |

## Status

The protocol, the service and the Android app are complete and work end to end: a Pi found,
its LED read, SPAKE2, a network joined, and the service removed from the phone.

| | Where it has been run |
| --- | --- |
| The service | Raspberry Pi 5 on 64-bit Raspberry Pi OS (trixie), with BlueZ and NetworkManager |
| The app | One Android 12 phone, in a dark room, at all three speeds |
| The package | Install, upgrade, removal, purge, and removal from the app, on a Pi 5 |

Nothing here has had an outside security review.

## Roadmap

- Google Play: what is left is in `docs/play-store.md`.
- Pairing in a lit room, and on more phones.
- Wi-Fi Easy Connect from the app: the service's side is there and tested.
- An iOS app.

## Contributing

`CONTRIBUTING.md` has the rules for changes, and `CODE_OF_CONDUCT.md` how everyone taking
part is expected to behave. A vulnerability is reported privately, as `SECURITY.md` says, not
in an issue.

## License

Apache License 2.0; see LICENSE and NOTICE.
