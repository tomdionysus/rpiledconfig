# Installing the daemon

The daemon ships as a Debian package, `rpiledconfig`, built on the Pi it is for (README.md,
Installing the service). A Raspberry Pi on 64-bit Raspberry Pi OS (trixie) is the reference host. It needs
BlueZ (`bluetooth.service`), NetworkManager and polkit (`polkitd`), all of which Raspberry Pi OS
ships and runs by default; the package depends on them.

## What the package installs

| Path | What it is |
| --- | --- |
| `/usr/bin/rpiledconfig` | The daemon. |
| `/usr/lib/systemd/system/rpiledconfig.service` | The unit: hardened, running as `rpiledconfig`. |
| `/usr/lib/systemd/system/rpiledconfig-uninstall.service` | A oneshot unit, run as root, that purges the package when an authenticated phone asks (docs/protocol.md, Uninstalling). Never enabled; started only through systemd. |
| `/etc/rpiledconfig/rpiledconfig.conf` | The configuration, a conffile: an upgrade never overwrites your changes. |
| `/usr/lib/udev/rules.d/60-rpiledconfig.rules` | Gives the `rpiledconfig` group write access to the activity LED's `brightness` and `trigger`. |
| `/usr/share/polkit-1/rules.d/60-rpiledconfig.rules` | The polkit rule that lets the `rpiledconfig` user, and only it, scan for, add, activate and delete NetworkManager connections, and start `rpiledconfig-uninstall.service`. |
| `/usr/share/man/man8/rpiledconfig.8.gz` | The manual page, `man rpiledconfig`. |
| `/usr/share/doc/rpiledconfig/` | These docs, the protocol, the example configuration, README and NOTICE, and the licence in `copyright`. |

The daemon keeps no state: its pairing key is in memory only. An upgrade from a version
that kept a machine ID deletes `/var/lib/rpiledconfig`.

The package also creates the system user and group `rpiledconfig` (no login, no home),
and the system group `netdev` if the system has none
(Raspberry Pi OS has one). There is no D-Bus policy: the distributions' own already allow
everything the daemon does (below).

## Installing

Install the package you built with apt, which fetches the dependencies:

    sudo apt install ./build-deb/rpiledconfig_<version>_arm64.deb

There is no 32-bit (armhf) build.

To keep a configuration you already have without a question from dpkg, for example in a script:

    sudo apt -o Dpkg::Options::=--force-confold install ./build-deb/rpiledconfig_<version>_arm64.deb

### Replacing a `cmake --install` installation

A unit `cmake --install` put under `/usr/local` takes precedence over the package's. Remove what
it installed first, from the build directory it was run in:

    sudo systemctl disable --now rpiledconfig
    xargs -d '\n' sudo rm -f < install_manifest.txt
    sudo systemctl daemon-reload

`install_manifest.txt` does not list `/etc/rpiledconfig/rpiledconfig.conf`, which stays and is
kept by the package. `dpkg -S /usr/bin/rpiledconfig` names the package that owns the binary.

The unit is enabled and started, and advertises over Bluetooth whether the Pi has WiFi or not
(docs/security.md says what that exposes). With `setup.always: false` a Pi on WiFi idles, checking
its connection, and advertises only once it has none.

## First run

Watch it:

    sudo journalctl -u rpiledconfig -f

In setup mode, `bluetoothctl` on another machine, or any BLE scanner, sees
`RPiLEDConfig-<4 hex>` (the prefix is `ble.name_prefix`; the name is in the scan response, so a
scanner has to scan actively, as phones do). The journal says `GATT application registered` and
`Advertising RPiLEDConfig-<4 hex>`; if it says BlueZ refused either, the reason follows.

The LED keeps doing what it did until a phone asks: open the app and pick the Pi by its name. The
daemon draws a one-time key on that connection and the activity LED starts blinking it (the
journal says `A new pairing key` and `Blinking the key`). Point the camera at it and follow the
app. The LED goes back to its usual trigger once the phone has authenticated, or two minutes
after it asked. Advertising carries on after `finish`. With `setup.always: false` it stops on
`finish` or after `setup.timeout`.

The journal never shows the key and no command prints it: it is the pairing secret. The name's 4
hex characters are unrelated to it.

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
- **systemd**, for uninstalling from the phone: the same rule lets `rpiledconfig` start
  `rpiledconfig-uninstall.service` (`org.freedesktop.systemd1.manage-units`, verb
  `start`, that unit only) and nothing else of systemd's. The unit runs as root and
  purges the package. `uninstall.driver: off` turns this off; the app is then told
  `uninstall_unavailable`.
- **wpa_supplicant**, for Wi-Fi Easy Connect: its control socket
  `/run/wpa_supplicant/wlan0` is `root:netdev 0770` where NetworkManager starts it, and
  the unit gives the service the `netdev` group (`SupplementaryGroups=netdev`) and
  nothing else of it. The daemon's end of the socket is an abstract name the kernel
  picks, so nothing is written under `/run` or `/tmp`. To check that the socket is there
  and allows DPP, as a member of `netdev`:

      wpa_cli -i wlan0 dpp_bootstrap_gen type=qrcode chan=81/6

  answers a number (then `wpa_cli -i wlan0 dpp_bootstrap_remove <number>`); `FAIL` or
  `UNKNOWN COMMAND` means this wpa_supplicant cannot do Easy Connect, and the daemon
  answers `easyconnect_unavailable`. The app does not offer Easy Connect.

A `cmake --install` into `/usr/local` puts the rule in
`/usr/local/share/polkit-1/rules.d`, which polkitd does not read: copy it to
`/etc/polkit-1/rules.d/60-rpiledconfig.rules`.

## Reconfiguring a Pi that already has WiFi

The app can change a Pi's WiFi at any time, as the service is advertising. If it has been set to
`setup.always: false`, either set it back and restart the service, or run the daemon by hand for
one session:

    sudo systemctl stop rpiledconfig
    sudo -u rpiledconfig rpiledconfig --config /etc/rpiledconfig/rpiledconfig.conf

and stop it with Ctrl-C.

## Upgrading

    sudo apt install ./build-deb/rpiledconfig_<new version>_arm64.deb

The configuration is kept; dpkg asks only when you changed it and the packaged default
changed too. The service restarts unless it has been disabled.

## Removing

    sudo apt remove rpiledconfig          # keeps /etc/rpiledconfig
    sudo apt purge rpiledconfig           # removes that too

Or from the phone: once paired, the app's "Remove RPi LED Config from this Pi" asks the
daemon to start `rpiledconfig-uninstall.service`, which waits 3 seconds and runs
`apt-get purge rpiledconfig`. The app cannot see how that ends. The WiFi connections the daemon
saved are kept either way. The journal of the removal:

    sudo journalctl -u rpiledconfig-uninstall
