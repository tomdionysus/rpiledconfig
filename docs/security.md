# What is exposed

With `setup.always: true`, the default, a Pi with this package installed answers over Bluetooth for
as long as it runs. This is what that offers, to whom, and what it does not protect against.

## What someone can do

**Anyone in Bluetooth range, with no key:**

| They can | The result |
| --- | --- |
| See the advertisement | The name, `<prefix>-<4 random hex>`, and that this service runs here |
| Connect | The Bluetooth adapter's name, by default the Pi's hostname, unless `ble.hide_adapter_name` is on |
| Read Info | The protocol, the service's version, and whether the LED is blinking |
| Read Challenge, write Auth | A guess at the key, of 2^64 possibilities |
| Write Blink | Make the LED blink, change its rate, or stop it |

**With the key, which takes sight of the LED and Bluetooth range together:**

| They can | The result |
| --- | --- |
| `status`, `wifi.scan` | The hostname unless `status.report_hostname` is off, the network the Pi is on, its IP address, and the networks it can see |
| `wifi.configure` | Move the Pi to any network, including one of theirs |
| `wifi.forget` | Delete the networks this service saved. Others stay |
| `uninstall` | Remove this service |

Anyone who can see the LED can ask for it to blink. So **sight of the LED and Bluetooth range are the
only gate on changing a Pi's network.** That includes sight through a window or a long lens, and a
Bluetooth range that a directional antenna extends.

## What protects it

- **The key:** 64 random bits, in memory only, for one pairing, gone after at most 15 minutes. It
  never goes over Bluetooth.
- **Guessing:** SPAKE2 gives a recording nothing to test guesses against offline. Live guesses are
  limited to 3 on a connection, and 5 in a row pause advertising for 60 seconds.
- **The channel:** after authentication every request and answer is AES-256-GCM under a key made for
  that authentication, and a request before authentication drops the link.
- **The service:** it runs as its own user with no capabilities, under the unit's sandbox. It can
  change only what polkit and the udev rule allow: NetworkManager connections, the LED, and starting
  the one unit that removes the package.
- **What is not reachable:** nothing the service offers reads a saved WiFi passphrase, or runs
  anything on the Pi beyond that one unit.
- **The program itself:** what comes over the radio is parsed by code held to bounds, run against
  arbitrary input by the fuzzers in `daemon/tests/fuzz`. The package is built with stack protection,
  fortified libc calls, checked container access, full RELRO and branch protection.

## What it does not protect against

- **Denial of service, by anyone in range.** Repeated bad guesses keep advertising paused. Writes to
  Blink can stop the LED or change its rate during someone's read. A central that connects and
  neither reads nor writes is not timed out, as the 30 seconds starts at its first read or write.
  None of these reveal anything, but each can stop a Pi being set up.
- **A person who can see the LED.** See above.
- **Being noticed.** A change of network or a removal is recorded only in the Pi's own journal, and
  nothing tells the owner. A removal deletes the service but not its journal.
- **A radio that stays on.** Bluetooth stays on and advertising for as long as the service runs. The
  app can remove the service once a Pi is settled, `service.stop_after_boot_minutes` stops it a set
  time after each boot, or `setup.always: false` makes it advertise only while the Pi has no WiFi
  connection.
