# Installing the daemon

The daemon ships as a Debian package, `rpiledconfig`, for arm64 and amd64, built for
Debian bookworm (Raspberry Pi OS bookworm) and trixie. A Raspberry Pi 4 or 5 on 64-bit
Raspberry Pi OS is the reference host. It needs BlueZ (`bluetooth.service`),
NetworkManager and polkit (`polkitd`), all of which Raspberry Pi OS ships and runs by
default; the package depends on them.

## What the package installs

| Path | What it is |
| --- | --- |
| `/usr/bin/rpiledconfig` | The daemon. The package depends on libssl3 and libsystemd0 from the distribution; Boost is headers only. |
| `/usr/lib/systemd/system/rpiledconfig.service` | The unit: hardened, running as `rpiledconfig`. |
| `/etc/rpiledconfig/rpiledconfig.conf` | The configuration, a conffile: an upgrade never overwrites your changes. |
| `/usr/lib/udev/rules.d/60-rpiledconfig.rules` | Gives the `rpiledconfig` group write access to the activity LED's `brightness` and `trigger`. |
| `/usr/share/polkit-1/rules.d/60-rpiledconfig.rules` | The polkit rule that lets the `rpiledconfig` user, and only it, scan for, add, activate and delete NetworkManager connections. |
| `/usr/share/doc/rpiledconfig/` | These docs, the protocol, the example configuration, README, LICENSE and NOTICE. |
| `/var/lib/rpiledconfig/` | State: the machine ID (`machine-id`, 0600). Made by the package, not shipped in it. |

The package also creates the system user and group `rpiledconfig` (no login, home
`/var/lib/rpiledconfig`), and the system group `netdev` if the system has none
(Raspberry Pi OS has one). There is no D-Bus policy: the distributions' own already allow
everything the daemon does (below).

## Installing

Copy the package for your suite and architecture to the Pi and install it with apt, which
fetches the dependencies:

    sudo apt install ./rpiledconfig_0.1.0-1~deb12_arm64.deb

`~deb12` is the bookworm build and `~deb13` the trixie one
(`grep VERSION_CODENAME /etc/os-release`). `dpkg --print-architecture` says `arm64` on
64-bit Raspberry Pi OS. There is no 32-bit (armhf) build.

The unit is enabled and started: the daemon's whole purpose is to be running on a Pi
that has not been configured yet. On a Pi that already has WiFi it starts, sees the
connection, and idles.

## First run

Watch it:

    sudo journalctl -u rpiledconfig -f

On a Pi with no WiFi connection it enters setup mode: the activity LED starts
blinking the machine ID, and `bluetoothctl` on another machine, or any BLE scanner, sees
`RPiLEDConfig-<fingerprint>` (the name is in the scan response, so a scanner has to scan
actively, as phones do). The journal says `GATT application registered` and
`Advertising RPiLEDConfig-<fingerprint>`; if it says BlueZ refused either, the reason
follows. Open the app, point the camera at the LED, and follow it.
When the app sends `finish`, or after `setup.timeout`, the LED goes back to its usual
trigger and advertising stops.

To see the machine ID the LED is blinking:

    sudo rpiledconfig --show-id

The journal shows only the fingerprint, never the ID: the ID is the pairing secret.

## What the service user is allowed

The daemon runs as `rpiledconfig`, with no capabilities, and gets each thing it needs
from the part of the system that owns it:

- **The LED**: the udev rule gives the `rpiledconfig` group `brightness` and `trigger`
  of `ACT` and `PWR` (or `led0` and `led1`) and nothing else under `/sys`.
- **BlueZ**: Debian's `bluetooth.conf` lets any user call BlueZ, and lets BlueZ (root)
  call back into the GATT application and advertisement the daemon exports. The daemon
  owns no bus name. If you have restricted `bluetooth.conf` to the `bluetooth` group,
  add `rpiledconfig` to that group, or allow it in a file of your own,
  `/etc/dbus-1/system.d/rpiledconfig.conf`:

      <busconfig>
        <policy user="rpiledconfig">
          <allow send_destination="org.bluez"/>
        </policy>
      </busconfig>

- **NetworkManager**: anyone may read devices, access points and connections over the
  bus; changing them is polkit's decision. The package's rule answers yes to
  `rpiledconfig` for these actions and leaves every other user to polkit's defaults:

  | Action | Why |
  | --- | --- |
  | `org.freedesktop.NetworkManager.settings.modify.system` | `wifi.configure` saves a system connection; `wifi.forget` deletes it |
  | `org.freedesktop.NetworkManager.network-control` | `wifi.configure` activates it |
  | `org.freedesktop.NetworkManager.wifi.scan` | `wifi.scan` asks for a fresh scan |

  polkitd picks the rule up as soon as it is installed. To check it, as root:

      sudo -u rpiledconfig nmcli general permissions

  should say `yes` for those three. Without the rule a configure is answered
  `wifi_failed` with "not authorized", and a scan falls back to the cached list.
- **wpa_supplicant**, for Wi-Fi Easy Connect: its control socket
  `/run/wpa_supplicant/wlan0` is `root:netdev 0770` where NetworkManager starts it, and
  the unit gives the service the `netdev` group (`SupplementaryGroups=netdev`) and
  nothing else of it. The daemon's end of the socket is an abstract name the kernel
  picks, so nothing is written under `/run` or `/tmp`. To check that the socket is there
  and allows DPP, as a member of `netdev`:

      wpa_cli -i wlan0 dpp_bootstrap_gen type=qrcode chan=81/6

  answers a number (then `wpa_cli -i wlan0 dpp_bootstrap_remove <number>`); `FAIL` or
  `UNKNOWN COMMAND` means this wpa_supplicant cannot do Easy Connect, and the app is told
  `easyconnect_unavailable` and offers its other two ways.

A `cmake --install` into `/usr/local` puts the rule in
`/usr/local/share/polkit-1/rules.d`, which polkitd does not read: copy it to
`/etc/polkit-1/rules.d/60-rpiledconfig.rules`.

## Reconfiguring a Pi that already has WiFi

Set `setup.always: true` in `/etc/rpiledconfig/rpiledconfig.conf` and restart the
service; setup mode then stays on, and the app can change the WiFi at any time. Or run
the daemon by hand for one session:

    sudo systemctl stop rpiledconfig
    sudo -u rpiledconfig rpiledconfig --config /etc/rpiledconfig/rpiledconfig.conf

and stop it with Ctrl-C. Run that way it does not have the unit's `netdev` group, so
Wi-Fi Easy Connect answers `easyconnect_unavailable` ("not allowed to use
/run/wpa_supplicant/wlan0"); everything else works. With it:

    sudo systemd-run --pty --uid=rpiledconfig --gid=rpiledconfig -p SupplementaryGroups=netdev \
        /usr/bin/rpiledconfig --config /etc/rpiledconfig/rpiledconfig.conf

## A new machine ID

    sudo systemctl stop rpiledconfig
    sudo -u rpiledconfig rpiledconfig --regenerate-id
    sudo systemctl start rpiledconfig

An app that learnt the old ID has to read the LED again.

## Upgrading

    sudo apt install ./rpiledconfig_<new version>_arm64.deb

The configuration is kept; the service restarts if it was running.

## Removing

    sudo apt remove rpiledconfig          # keeps /etc/rpiledconfig and the machine ID
    sudo apt purge rpiledconfig           # removes those and the service user too
