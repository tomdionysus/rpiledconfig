# Configuration

The daemon reads one file, in this order: `--config PATH`, then
`$RPILEDCONFIG_CONFIG`, then `/etc/rpiledconfig/rpiledconfig.conf`. The example with
every key is `daemon/config/rpiledconfig.example.conf`, installed under
`/usr/share/doc/rpiledconfig/`.

The format is one `key: value` per line. Keys are dotted. `#` starts a comment, blank
lines are ignored. A bare value ends at ` #`; a value that has to carry a space followed
by `#` goes in double quotes, inside which `\n`, `\t`, `\\` and `\"` are the escapes.
Booleans are `true` or `false`, nothing else. A key set twice is an error, and so is an
unknown key, and both name their line: a misspelt key that silently fell back to a
default would be the daemon blinking the wrong LED. Relative paths are relative to the
file, not to where the daemon was started.

| Key | Default | Meaning |
| --- | --- | --- |
| `log.level` | `info` | `debug`, `info`, `warn` or `error`. |
| `state.dir` | `/var/lib/rpiledconfig` | Where `machine-id` lives. |
| `led.driver` | `sysfs` on Linux, `fake` elsewhere | `sysfs` drives a kernel LED; `fake` logs what it would do. |
| `led.path` | `/sys/class/leds/ACT` | The LED's sysfs directory: `ACT`, the activity LED (`led0` on Raspberry Pi 3 and earlier), or `PWR`, the power LED (`led1` on older boards). |
| `led.active_low` | `false` | `true` when the LED lights at brightness 0 (the Raspberry Pi 5's `ACT`); without it the camera reads every frame inverted. |
| `ble.driver` | `bluez` on Linux, `fake` elsewhere | `bluez` serves GATT through BlueZ over D-Bus; `fake` serves nothing and logs. |
| `ble.adapter` | `hci0` | The BlueZ adapter to advertise on, by its name under `/org/bluez` (`ls /sys/class/bluetooth`). Powered on at start if it is off. |
| `wifi.driver` | `networkmanager` on Linux, `fake` elsewhere | `networkmanager` over D-Bus; `fake` answers with made-up networks. |
| `wifi.interface` | `wlan0` | The WiFi interface: its wpa_supplicant control socket (`/run/wpa_supplicant/<interface>`) and its MAC in the Easy Connect URI. |
| `wifi.easyconnect` | `fake` when `wifi.driver` is `fake`, else `wpa_supplicant` | Wi-Fi Easy Connect: `wpa_supplicant` (Linux only), `fake` (a made-up URI; nothing ever arrives), or `off` (the app is told `easyconnect_unavailable`). |
| `setup.always` | `false` | Stay in setup mode whatever the WiFi state. |
| `setup.timeout` | `900` | Seconds of setup mode before giving up until the next recheck. |
| `setup.recheck` | `60` | Seconds between WiFi checks outside setup mode. |

`fake` drivers are how the daemon is developed and tested on a machine that is not a Pi.
On a Pi they are useful one at a time: `wifi.driver: fake` lets the LED and BLE be tried
without touching the Pi's real network, and makes Wi-Fi Easy Connect fake too unless
`wifi.easyconnect` says otherwise, because Easy Connect listens with the WiFi radio.

The `bluez` and `networkmanager` drivers share one connection to the system bus. The
daemon refuses to start if that connection cannot be made; BlueZ or NetworkManager not
answering is logged and reported to the app (`wifi_unavailable`), not fatal.

## BLE through BlueZ

While setup mode is on the daemon exports a GATT application at
`/com/tomcully/rpiledconfig` (one service, the six characteristics of
docs/protocol.md) and an advertisement at
`/com/tomcully/rpiledconfig/advertisement0`, on its own bus connection, and registers
both with BlueZ on `ble.adapter`. They are unregistered when setup mode ends, and BlueZ
drops them by itself if the daemon dies. To see them while it runs:

    busctl --system get-property org.bluez /org/bluez/hci0 org.bluez.LEAdvertisingManager1 ActiveInstances

A phone becomes a connection on its first read or write of the service, not when BlueZ
reports a device connected, so a Bluetooth keyboard or speaker already paired to the Pi
is never mistaken for a phone or disconnected. A second phone that reads or writes while
one is being served is disconnected (`Device1.Disconnect`).

## WiFi through NetworkManager

`wifi.configure` saves a system connection named `rpiledconfig-<ssid>` (for example
`rpiledconfig-Home`), WPA-PSK when a passphrase was given and open when not, with
autoconnect on and the passphrase stored by NetworkManager, so it comes back after a
reboot with nobody logged in. Earlier `rpiledconfig-` connections are deleted once the
new one is saved, so a wrong passphrase tried first does not linger.

That name is how the daemon recognises its own connections: `wifi.forget` deletes every
saved connection whose name starts with `rpiledconfig-`, and nothing else. A connection
made by hand, by Raspberry Pi Imager (`preconfigured`, or `netplan-wlan0-...` on
netplan-based images) or by `nmcli` is never touched, and renaming one of ours with
`nmcli connection modify rpiledconfig-Home connection.id Home` takes it out of the
daemon's hands.

`wifi.scan` asks NetworkManager for a fresh scan and waits up to 10 seconds for it; if
NetworkManager refuses (it rate-limits scans, and polkit may say no) the answer is the
access points it already knows.

NetworkManager decides what the service user may change through polkit; the package's
rule is described in docs/installation.md. A Pi with no WiFi device answers
`wifi_unavailable`, as when NetworkManager is not there.

## Wi-Fi Easy Connect through wpa_supplicant

`wifi.easyconnect: wpa_supplicant` makes the daemon an Easy Connect (DPP) enrollee
through the control socket of the wpa_supplicant NetworkManager runs for
`wifi.interface` (docs/protocol.md, "Copying the phone's WiFi"). On `wifi.easyconnect`
it sends `STATUS` (for the interface's MAC), `DPP_BOOTSTRAP_GEN type=qrcode
chan=81/1,81/6,81/11 mac=<mac>`, `DPP_BOOTSTRAP_GET_URI` and `DPP_LISTEN 2437
role=enrollee`, answers the URI, and listens for 120 seconds. The network the phone
sends arrives as wpa_supplicant's `DPP-CONFOBJ-SSID` and `DPP-CONFOBJ-PASS` (or `-PSK`)
events and is saved through NetworkManager exactly as `wifi.configure` saves one, as
`rpiledconfig-<ssid>`. wpa_supplicant itself saves nothing (`dpp_config_processing`
stays 0). On success, after 120 seconds, on `wifi.easyconnect.stop`, or when the phone's
connection closes, the daemon sends `DPP_STOP_LISTEN` and `DPP_BOOTSTRAP_REMOVE`.

What it cannot apply: a network configured for DPP alone (a connector and no
passphrase), which NetworkManager cannot be given this way; it is logged and nothing
changes. A WPA3-only (SAE) network arrives with its passphrase and is saved as WPA-PSK,
which such a network refuses; WPA2 and WPA2/WPA3 transitional networks join.

Where NetworkManager may get in the way. It drives the same wpa_supplicant, and in setup
mode there is no WiFi connection, so it is scanning for one to autoconnect to. A scan
takes the radio off channel 6 while it runs, and wpa_supplicant queues the listen and
the scans one after another, so the listen has gaps in which the phone's first DPP
message can be missed. The phone repeats it, so this costs time rather than the
exchange, but it is the first thing to suspect if Easy Connect is slow or never starts.
An autoconnect attempt to a saved network that comes into range moves the radio the
same way.

The journal shows each step: `Wi-Fi Easy Connect: listening on 2437 MHz`, `the phone
authenticated`, `a configuration arrived`, `received <ssid>`, or the reason it stopped.
