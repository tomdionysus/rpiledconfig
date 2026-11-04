# The RPiLEDConfig protocol

Version 3 (version 1 authenticated with an HMAC challenge; version 2 with
SPAKE2; version 3 blinks only when a phone asks, and can uninstall itself). This is the contract between the daemon (`daemon/`) and the phone
app (`app/`). Change this document first, then both sides; the constants here
are the constants in `daemon/src/protocol/` and `app/src/protocol/`, and the
tests on each side are written from this text.

## The idea

Setting up a Raspberry Pi needs two things a stranger in the next room cannot
have: line of sight to its LED, and Bluetooth range. The daemon generates a
64-bit machine ID and offers a BLE service that answers only to someone who
knows that ID. The phone finds the Pi over BLE and asks it to blink; the
daemon blinks the ID out on the Pi's activity LED; the phone reads the LED
with its camera, proves knowledge of the ID over BLE, and then configures
WiFi over an encrypted channel keyed from it.

In the app, in order:

1. Scan for BLE peripherals named `RPiLEDConfig-<fingerprint>` and list
   them; the user picks the Pi to set up (each is shown by its
   fingerprint).
2. Connect to it, write 0x01 to Blink (BLE, below) and disconnect. The Pi's
   LED starts blinking its ID, and only that Pi's: a room of Pis in setup
   mode does not blink at all until somebody asks one.
3. Read the LED. An ID whose fingerprint is not the chosen Pi's is another
   Pi's LED, and is not used.
4. Connect again, authenticate, configure WiFi. The accepted authentication
   stops the blinking.

Nothing is typed. Nothing is printed on the board. The ID never crosses the
air in the clear: the LED carries it as light, and BLE carries only SPAKE2
shares, key confirmations and ciphertext.

## The machine ID

- 64 bits from the kernel's random source (`getrandom(2)`, blocking until
  seeded). The package generates it when it is installed; without the
  package (or with `state.dir` moved), the daemon's first start does.
- Kept in `<state dir>/machine-id` as 16 lowercase hex characters and a
  newline, mode 0600. A later start reads it back; `rpiledconfig --show-id`
  prints it.
- It is written whole or not at all: to a temporary file in the same
  directory, synced, then renamed over `machine-id`. An empty `machine-id`
  (which only a write cut short by a power loss can leave) counts as no ID,
  and the next start generates one. Any other content that is not 16 hex
  characters (a 6- or 12-character ID from frame version 4 or 3 included)
  is an error the daemon will not start past; `--regenerate-id` replaces it.
- `--regenerate-id` replaces it. The old one is gone for good, and an app that
  learnt it has to read the LED again.
- Its byte form, `id_bytes`, is the 8 bytes of the ID in big-endian order.
  Every derivation below starts from `id_bytes`.
- Why 64 bits: the advertisement carries a 16-bit fingerprint of the ID
  (Keys, below), which anyone in radio range can see. Frame version 4's
  24-bit ID left 8 bits behind it, 256 guesses, which the live rate limit
  (5 refusals, then a minute's pause) lets through in under an hour. With
  64 bits, 48 stay unknown. Authentication is a PAKE (SPAKE2, below), so a
  recorded pairing gives nothing to test guesses against offline and every
  guess is a live, rate-limited attempt. On the LED it costs 7.65 seconds
  a frame (frame version 7).

## The LED channel (daemon to camera)

The LED is the Pi's activity LED by default (`/sys/class/leds/ACT`, or
`led0` on older boards), the main LED a user sees; the power LED (`PWR`,
or `led1`) can be chosen instead with `led.path`. While it blinks (a phone
asked for it with Blink, below), the daemon takes the LED's trigger over
(`none`), drives `brightness` itself, and restores the trigger it found when
the blinking stops, so the LED otherwise does what the Pi's own setting
says.

This is frame version 7. Version 1 was an uncoded 64-bit ID and CRC-8; 2 a
64-bit ID under RS(24, 12); 3 a 48-bit ID, RS(20, 10), T = 100 ms; 4 a
24-bit ID, RS(14, 7); 5 a 64-bit ID with magic, version and CRC under
RS(24, 12), Manchester at T = 50 ms, 19.9 seconds a frame; 6 this frame at
T = 50 ms, 8.5 seconds. Version 7 is version 6 at T = 45 ms.

### Symbols

The LED sends one symbol every **T = 45 ms**: on for a `1`, off for a `0`.
The decoder is expected to tolerate 5% clock error over a frame.

Why this rate: measured, on 2026-11-04, with a Blackview A85 at 30 fps (the
most its camera gives a frame processor) on a Raspberry Pi 5's LED, each
symbol time run in scripted rounds from the blink to the pairing: 50 ms
read 5.5 to 8.1 s after first sight; 45 ms 3.6 to 5.8 s, every round; 40 ms
3.6 to 11.4 s, slower than either neighbour; 33 ms 1.5 to 6.6 s in 13 of 13
rounds but with exactly one camera frame per symbol, no margin for a
slower camera or a brighter room; 25 ms worked with rounds of 22 and 30 s.
45 ms is the fastest with margin: a 30 fps camera samples each symbol 1.35
times. The edges blur by up to a camera frame.

The symbols are not the data bits. Each 4 bits of data (a nibble) is sent as
one of the 16 five-symbol words that hold no run of three equal symbols,
the nibble's value being the word's index in ascending order:

| Nibble | Word | Nibble | Word | Nibble | Word | Nibble | Word |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `00100` | 4 | `01010` | 8 | `10010` | 12 | `10110` |
| 1 | `00101` | 5 | `01011` | 9 | `10011` | 13 | `11001` |
| 2 | `00110` | 6 | `01100` | 10 | `10100` | 14 | `11010` |
| 3 | `01001` | 7 | `01101` | 11 | `10101` | 15 | `11011` |

A word begins and ends with a run of at most two, so wherever words meet the
LED holds one state for at most 4T, whatever the data: a camera always has
edges to keep time by, and a run of 5T or more can only be the sync. The
code carries 0.8 data bits per symbol, against Manchester's 0.5, which is
what makes version 6 fast.

### The frame

A frame is, in order:

| Part | On the LED | Symbols |
| --- | --- | --- |
| Sync | on for 5T, then off for 5T | 10 |
| Codeword | 16 bytes, each as its high nibble's word then its low nibble's, each word first symbol first | 160 |

Frames repeat back to back while the daemon blinks. One frame is 170T, 7.65
seconds. A word next to the sync can lengthen its runs (the last word's
final `1`s join the sync's on, the first word's leading `0`s its off), so
the sync's runs are 5T to 7T; nothing else is longer than 4T.

### The codeword

The 10-byte message is `id_bytes` followed by a CRC-16, big-endian, over the
frame version byte 0x07 and `id_bytes`. The version byte is not sent: it is
folded into the CRC, so a frame of another version can never pass, and it
costs no time on the LED.

The CRC is CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no
reflection, no final XOR. Its check value for the ASCII bytes of `123456789`
is 0x29B1.

The codeword is that message followed by 6 Reed-Solomon parity bytes:
RS(16, 10) over GF(2^8) with the primitive polynomial 0x11D, the generator
`g(x) = (x - a^0)(x - a^1)...(x - a^5)` with `a = 2`, systematic, message
first. It corrects any 3 wrong bytes, or any 6 bytes the decoder marks as
erased, or any mix with `2 x errors + erasures <= 6`.

The CRC is checked after the Reed-Solomon decode. It rejects a miscorrected
word (a pattern beyond the code's reach that decodes to a different, wrong
codeword) and anything that is not this protocol's frame, such as another
blinking light; the words add a check of their own, as only 16 of the 32
five-symbol patterns are words.

Why this much redundancy: a phone held by hand loses symbols in bursts (a
hand shake, a reflection, a missed camera frame). A byte is two words, ten
symbols, half a second; the parity covers three such bytes damaged, or six
not seen, so a frame decodes through a second or two of trouble, or from a
little over 7 seconds of looking when the rest is missing.

### Decoding

As the app does it. The camera's frames go to a native frame-processor
plugin, which does all of what follows on the camera's thread and hands the
app only its state.

Finding the LED. The plugin divides each frame's luminance plane into tiles
and keeps the last second of every tile's brightness (the mean of its
brightest pixels, so a small LED is not averaged away in a lit room). A tile
holds this protocol's LED when, over that second, its level (an adaptive
threshold midway between a running minimum and maximum that decay over
about 2 seconds, with 15% hysteresis) changes in runs of 1T to 7T with a
camera frame's slack, at least four of them and none longer; and its
brightnesses sit at two levels: the distance between the medians of the
samples above and below the middle of the range is at least 4 times the sum
of their median absolute deviations, and at least half the samples lie
within 12% of the range of the 5th or 95th percentile (to keep an LED
already found, either one, at 2.5 or 0.45). Noise passes both only in
isolated windows, so a tile is taken only after passing for 800 ms on end.
The tile taken is the one that passes with the greatest contrast, and the
decoder reads it until it stops passing; the user can point the camera
roughly and need not centre the LED.

Reading. The frame repeats unchanged, so the stream is cyclic and every
sample, wherever the camera came in, belongs to one known symbol of the
frame. The decoder does not look for edges or wait for a sync. Once it holds
most of a frame of samples, and then every half second, it folds them onto
one cycle at clock rates across the 5% tolerance and scores every start: the
sync's ten symbols must agree, and each data symbol adds how decisive it is.
The best few starts are refined in rate and start and read out: each word's
five symbols, weighted towards the middle of each symbol, are matched
against the 16 words, and the best is its nibble, with the margin over the
next best as its confidence. Every cycle's samples add together, and so do
the samples before the first sync the camera saw, which are the tail of the
same codeword; the latest frame period alone is read as well, so that older
samples from a bad moment cannot hold a read back.

Then:

- A byte whose words are uncertain, or not yet seen, is marked erased
  rather than guessed, which doubles what the code can repair.
- Decode with errors and erasures, then check the CRC.

One frame that passes is a read: in under one frame, 7.65 seconds, from
whenever the camera first sees the LED, and sooner when the code can fill
what is missing. The progress shown is only what was measured: nothing until
the LED is found, then the share of the frame's symbol slots a sample has
fallen in. The fingerprint is shown so the user can see that it matches the
Pi they are pointing at.

## Keys

All hashes are SHA-256. HKDF is HKDF-SHA256 (RFC 5869).

| Name | Derivation |
| --- | --- |
| `w` | HKDF(ikm = `id_bytes`, salt = ASCII `RPiLEDConfig`, info = ASCII `spake2-w v2`, L = 40), as a big-endian integer, mod `n` (the order of P-256) |
| `fingerprint` | the first 2 bytes of SHA-256(`id_bytes`), as 4 lowercase hex characters |
| `S` | HKDF(ikm = `Ke`, salt = empty, info = ASCII `session-key v2`, L = 32) |

`w` is the SPAKE2 password scalar, the same for the life of the machine ID;
40 bytes reduced mod `n` keeps the bias below 2^-64 (RFC 9382, section 3.2).
`Ke` is the shared secret of one SPAKE2 run (below), and `S` is that
session's key. The fingerprint is public: it is in the advertisement so the
app can tell which Pi is which, and it is shown to the user so they can see
that the Pi they are talking to is the one whose LED they read.

## BLE

The daemon advertises while setup mode is on, and stops when it is off. The
LED is dark (left to its trigger) until a phone writes Blink.

- Local name: `RPiLEDConfig-<fingerprint>`, for example `RPiLEDConfig-3a9c`.
- Advertised service UUID: the service below.
- Connectable, general discoverable for as long as setup mode lasts (the
  advertisement's discoverable timeout is 0; BlueZ's default of 180 s
  hid the Pi three minutes in). No BLE-level bonding or pairing is used
  or requested: the security is the application layer below, so the link
  works the same on every phone and every BlueZ.

Service UUID `2a7f0001-7e1d-4c3a-9b5e-0f8a6d4c2b10`. Characteristics share the
base and differ in the first group:

| UUID (first group) | Name | Properties | Content |
| --- | --- | --- | --- |
| `2a7f0002` | Info | read | plain UTF-8 JSON: `{"protocol":3,"version":"<daemon version>","fingerprint":"<4 hex>"}` |
| `2a7f0003` | Challenge | read | `pB`: the daemon's SPAKE2 share, 65 bytes, from a new `y` for every connection and after every Auth write |
| `2a7f0004` | Auth | write | `pA` (65 bytes) || `cA` (32 bytes) |
| `2a7f0005` | AuthResult | read, notify | one byte: 0x01 accepted, 0x00 refused; on 0x01 followed by `cB` (32 bytes) |
| `2a7f0006` | Request | write | a fragment of an encrypted message, app to daemon |
| `2a7f0007` | Response | notify | a fragment of an encrypted message, daemon to app |
| `2a7f0008` | Blink | write | one byte: 0x01 blink the ID on the LED, 0x00 stop |

### Blink

Anyone connected may write Blink; it needs no authentication. Blinking shows
the ID only to someone who can see the LED, which is the first factor
itself, so asking for it gives a stranger out of sight nothing; what it
changes is that a Pi does not show its ID to the room when nobody is setting
it up.

- 0x01 starts the LED's frames from the sync, or, while it already blinks,
  restarts its clock.
- 0x00 stops it.
- Anything else, a write that is not one byte included, is ignored.

The LED stops, and its trigger is given back, on the first of: an accepted
authentication on any connection, a 0x00 write, 120 seconds after the last
0x01, and setup mode ending. Disconnecting does not stop it: the app writes
Blink, disconnects, reads the LED, and connects again to authenticate.

A daemon whose Info says protocol 2 has no Blink and blinks for as long as
setup mode is on; an app skips the write for it.

### Authentication

Authentication is SPAKE2 (RFC 9382) with the ciphersuite
SPAKE2-P256-SHA256-HKDF-HMAC, the password scalar `w` above, the app as A
(it uses `M`) and the daemon as B (it uses `N`). `M` and `N` are the P-256
points of RFC 9382, section 6. The identities A and B are empty: the roles
are fixed by the protocol, so they are implicit (RFC 9382, section 3.3).
AAD is ASCII `RPiLEDConfig auth v2`. Points are SEC1 uncompressed, 65 bytes.

The point of a PAKE: a recording of a pairing, or of a failed one, gives an
attacker nothing to test guesses of the ID against offline. Each guess costs
a live attempt at the daemon, and the daemon limits those (below). A fake
Pi gets one guess per run the user starts, as with any PAKE.

1. The app connects, reads Info and checks the fingerprint against the ID it
   read from the LED. A mismatch is shown, and the app goes no further.
2. The app reads Challenge to get `pB = y*P + w*N`. The daemon drew `y`
   uniformly from [1, n).
3. The app draws `x` the same way, computes `pA = x*P + w*M`,
   `K = x*(pB - w*N)`, the transcript `TT`, `Ke || Ka = SHA-256(TT)`,
   `KcA || KcB = HKDF(ikm = Ka, salt = empty, info = "ConfirmationKeys" || AAD, L = 32)`
   and `cA = HMAC-SHA256(KcA, TT)`, all exactly as RFC 9382 sections 3.3
   and 4 give them, and writes Auth with `pA || cA`.
4. The daemon checks that `pA` is a point on P-256 and not the identity,
   computes `K = y*(pA - w*M)` (refusing the identity), `TT` and the keys
   the same way, and compares `cA` in constant time. On a match it derives
   `S`, and AuthResult becomes `0x01 || cB` with `cB = HMAC-SHA256(KcB, TT)`.
   On a mismatch AuthResult becomes `0x00`, a new `y` (so a new `pB`) is
   drawn, and the attempt is counted. A write that is not exactly 97 bytes,
   or whose `pA` is not a valid point, is a mismatch.
5. The app checks `pB` was a valid point (step 3), verifies `cB`, and only
   then uses `S`. Now both sides know the other holds `w`, and both hold `S`.

`x` and `y` are never reused: `y` is renewed with `pB`, and the app draws a
new `x` for every Auth write. The Auth write is 97 bytes, so it needs an ATT
MTU of at least 100; the app asks for 517 before authenticating (the A85
settled on 185). A phone that grants less cannot authenticate, and the app
says so rather than writing a truncated Auth.

Before any attempt on a connection AuthResult reads as empty, so "refused"
is never reported for an attempt that did not happen.

AuthResult's notification only says that it changed: at the default ATT MTU
a notification carries 20 bytes and `0x01 || cB` is 33, so BlueZ
truncates it. The app reads AuthResult after the notification. Reads of
Info, Challenge and AuthResult may be long (several ATT reads at
increasing offsets); writes are never long, and a write at a non-zero
offset is refused.

A read of Challenge on a connection that has already authenticated starts
over: the session forgets `S`, draws a new `y` and `pB`, and waits for an Auth
write as on a new connection. A phone operating system can keep a BLE link
open after the app that used it lets go (macOS does, for half a minute), so
an app that starts again may find itself on an old, authenticated link; it
always begins by reading Challenge, and this rule makes that a fresh start.

A connection with three refused attempts is dropped. Five refused attempts in
a row across connections pause advertising for 60 seconds; a blink already
asked for carries on. The count resets on an accepted attempt and when the pause ends.
A connection that has not authenticated within 30 seconds is dropped. A
Request before AuthResult is 0x01 is ignored and the connection dropped.

The daemon serves one central at a time; a second connection while one is
open is disconnected without a challenge.

### The encrypted channel

Every message after authentication is AES-256-GCM under `S`.

- Nonce (12 bytes): a 4-byte direction, `00 00 00 01` for app to daemon and
  `00 00 00 02` for daemon to app, followed by an 8-byte big-endian counter.
  Each direction's counter starts at 0 and goes up by one per message.
- Associated data: none.
- Wire form of a message: `counter` (8 bytes, big-endian) || ciphertext ||
  GCM tag (16 bytes).
- A receiver accepts any counter on the first message of a direction (0 is
  the expected one), and after that rejects a counter less than or equal to
  the last one it accepted in that direction. A gap is allowed: a lost
  message is not a replay. The connection is dropped on a rejected counter
  and on any failure of the GCM tag.

### Fragments

A BLE write or notification carries at most the ATT MTU less 3 bytes, which
is 20 at the default ATT MTU of 23. That number is the largest fragment, the
flags byte included; a sender may use smaller ones, and a receiver takes
fragments of any size up to it. The daemon never sends a fragment larger
than an MTU of 185 allows (182 bytes), whatever BlueZ reports: on a Blackview
A85 BlueZ reported 517 while Android had settled on 185, and every larger
notification was dropped without a word. A message is sent as fragments:

- Byte 0 is flags: bit 0 set on the last fragment of a message; the other
  bits are 0 and a receiver refuses a fragment with any of them set.
- The rest, up to fragment size less 1 bytes, is the next slice of the
  message's wire form.
- Fragments of one message are sent in order and are never interleaved with
  another message's in the same direction.

A message is reassembled and then decrypted. A message longer than 65536
bytes is refused. So "fragments at a 20-byte MTU" in the vectors means
20-byte writes: 19 bytes of wire form in each but the last.

### Messages

Plaintexts are UTF-8 JSON objects. A request carries `op`; a response carries
`ok`, and when `ok` is false an `error` code and a `message` for a person.

| Request | Response when ok |
| --- | --- |
| `{"op":"status"}` | `{"ok":true,"hostname":"...","wifi":{"connected":true,"ssid":"...","ip":"...","join":{...}}}`, `wifi.connected` false with no `ssid` or `ip` when it is not. When the WiFi backend cannot be asked at all the answer is an error (`wifi_unavailable`), not `connected: false`. `join` (The latest join, below) is present once a `wifi.configure` has been taken since the daemon started |
| `{"op":"wifi.scan"}` | `{"ok":true,"networks":[{"ssid":"...","signal":0..100,"secure":true},...]}` sorted by signal, strongest first, ties by the SSID's UTF-8 bytes ascending, one entry per SSID (the strongest), hidden networks (empty SSID) left out, signal clamped to 0..100 |
| `{"op":"wifi.configure","ssid":"...","psk":"...","hidden":true}` | `{"ok":true}` once the connection is saved and activation has been asked for; `psk` is absent or empty for an open network; `hidden` is optional, true for a network typed by hand that may not broadcast its SSID (the Pi then probes for it by name) |
| `{"op":"wifi.forget"}` | `{"ok":true}` |
| `{"op":"finish"}` | `{"ok":true}`; the daemon leaves setup mode and disconnects |
| `{"op":"uninstall"}` | `{"ok":true}` once the removal has been started; the daemon disconnects, and the package is purged (Uninstalling, below) |

Error codes: `bad_request` (not JSON, no `op`, missing field), `unknown_op`,
`wifi_unavailable` (no backend, or NetworkManager is not answering),
`wifi_failed` (the backend refused; `message` says why), `busy`,
`easyconnect_unavailable`, `uninstall_unavailable`. A Pi with no WiFi device answers
`wifi_unavailable`.

`wifi.configure` answering `ok` means the connection is saved and
activation has been asked for, not that the network was joined. What became
of it is in `status`:

#### The latest join

`wifi.join` describes the most recent `wifi.configure` (or network received
by Wi-Fi Easy Connect) since the daemon started, and is absent before any:

| Field | Content |
| --- | --- |
| `ssid` | the network asked for |
| `state` | `joining` until the backend has said; then `joined`, or `failed` |
| `reason` | with `failed` only: `wrong_passphrase` (the network refused the passphrase), `not_found` (no network of that name was found), or `other` |
| `message` | with `failed` only: the backend's own words, for a person |

The daemon learns the outcome from NetworkManager as it happens (the WiFi
device's state becoming failed, with its reason, or the connection becoming
activated), so an app polling `status` learns a wrong passphrase or a
misspelt name within a few seconds of the backend giving up, rather than
at its own deadline. A join that NetworkManager abandons without a reason
the daemon can name is `other`.

A request the daemon does not understand gets `unknown_op`, so a newer app
against an older daemon learns what it cannot do rather than hanging.

### Uninstalling

`uninstall` removes RPiLEDConfig from the Pi for good, from the phone, so a
Pi that has been set up need not keep a service that answers anyone in
Bluetooth range. Only an authenticated phone can ask, so only someone who
read the LED. The app asks the user to confirm first, saying what goes.

The daemon runs as an unprivileged user and cannot remove a package, so the
package ships a oneshot unit, `rpiledconfig-uninstall.service`, that runs as
root and purges it, and a polkit rule that lets the service's user start
that one unit and nothing else. On `uninstall` the daemon asks systemd to
start the unit (`StartUnit`, mode `replace`) and answers `ok` once systemd
has taken the job, then disconnects. The unit waits 3 seconds, so the answer
reaches the phone before the daemon is stopped, and runs `apt-get purge
rpiledconfig`: the daemon stops and gives the LED back, and the program,
its units, its udev and polkit rules, its configuration and the machine ID
are deleted. The WiFi connections it saved are kept, so the Pi stays on the
network it was set up on.

`uninstall_unavailable` answers a daemon that cannot: one not installed
from the package (no such unit), one whose `uninstall.driver` is `off`, or
a refusal from systemd or polkit (`message` says which).

## Copying the phone's WiFi: Wi-Fi Easy Connect

No app can read a saved WiFi password on Android 10 and later or on iOS. On
Android 10 and later the operating system can send the phone's network to
another device itself, by Wi-Fi Easy Connect (DPP), after the user
confirms; the app never sees the password. The daemon is the enrollee, and
the BLE channel is how the app learns the Pi's bootstrap code, so only a
phone that has authenticated with the LED's ID can start it.

The daemon implements this; the 0.3.0 app does not offer it. On the
Blackview A85 the configurator took the phone off its network and never
reached the Pi listening on the Pi's own channel, and until that is
understood the app joins networks by passphrase or QR code only.

| Request | Response when ok |
| --- | --- |
| `{"op":"wifi.easyconnect"}` | `{"ok":true,"uri":"DPP:C:...;M:...;V:2;K:...;;"}` |
| `{"op":"wifi.easyconnect.stop"}` | `{"ok":true}` |

On `wifi.easyconnect` the daemon asks wpa_supplicant (through its control
socket) for a bootstrap key with wlan0's MAC and a channel list, returns
its URI, and listens for up to 120 seconds. When wlan0 is associated on a
2.4 GHz channel it listens there and lists that channel first, then 1, 6
and 11; otherwise it lists 1, 6 and 11 and listens on 2437 MHz (channel
6). Listening off the associated channel was not heard by an Android
configurator on a Raspberry Pi 5's Broadcom radio. wpa_supplicant runs the DPP exchange; with
`dpp_config_processing` left at 0 it saves nothing and reports the
received SSID and passphrase as events, and the daemon applies them
through the same path as `wifi.configure` (an `rpiledconfig-<ssid>`
connection in NetworkManager). Listening stops on success, after 120
seconds, on `wifi.easyconnect.stop`, or when the connection closes; the
bootstrap key is removed either way. The app then follows `status` exactly
as after `wifi.configure`.

The app hands the URI to Android with the intent action
`android.settings.PROCESS_WIFI_EASY_CONNECT_URI` and the URI as its data.
Android shows its own screen, the user picks the network (the current one
is offered first) and confirms, and Android sends it.

`easyconnect_unavailable` answers a daemon that cannot do this: no
wpa_supplicant control socket it may use, a wpa_supplicant built without
DPP, or a refusal. The app then offers the other two ways: scanning the
QR code of Android's own "Share" screen for a saved network (a
`WIFI:S:<ssid>;T:<WPA|nopass>;P:<passphrase>;;` code, which the app reads
and sends with `wifi.configure`), and typing.

## Setup mode

Setup mode is the BLE service advertising, and the LED blinking whenever a
phone has asked for it (Blink). The daemon
enters it at start when there is no WiFi connection (a backend that cannot
be asked counts as no connection), and stays in it until `finish` is
received or `setup.timeout` (default 15 minutes) passes. With
`setup.always: true` it never leaves: `finish` is still answered `ok` and
the phone disconnected, but the advertising carries on (and a blink
asked for, until it stops as Blink says). Outside
setup mode the daemon idles, checks WiFi every `setup.recheck` seconds
(default 60), and re-enters setup mode if the connection has gone.

Joining a network can drop the BLE link for a moment, because the Pi's WiFi
and Bluetooth share one radio. The app treats a disconnect after
`wifi.configure` as expected: it reconnects, authenticates again, and asks
for `status`. It finds the Pi again by its local name, not its address: a
phone may see a different BLE address for the same Pi after a reconnect.

`rpiledconfig --show-id` with no ID yet is an error, not a generation: the
ID is made by the daemon's first start. `--regenerate-id` always writes a
new one.

## Test vectors

The test suites on both sides include, at least:

- CRC-16/CCITT-FALSE of `123456789` is 0x29B1.
- For machine ID `0x0123456789abcdef`, computed independently of both sides
  (Python, from the definitions above, after it reproduced frame version
  6's vectors):
  - the message: `0123456789abcdef008c`
  - the codeword: `0123456789abcdef008cc420bb0f15c9`
  - the generator polynomial, highest power first: `013f01da20e326`
  - the frame's 170 symbols:
    `11111000000010000101001100100101010010110110001101100101001110100101011011011001110101101100100001001001010110101100101000110001001010110101001001101100101010111011010011`,
    107 runs, starting `+5,-7,+1,-4,+1,-1,+1,-2` and ending
    `-1,+3,-1,+2,-1,+1,-2,+2`. Runs are physical: adjacent symbols at the
    same level merge.
  - Frame version 6's message for the same ID was `0123456789abcdefebaf`
    and its codeword `0123456789abcdefebaf67a7a555b2c6`: the CRC is what
    tells the versions apart.
- The decoder recovers that codeword with 3 bytes changed, with 6 bytes
  erased, and with 2 changed and 2 erased; and rejects a word whose CRC is
  wrong, including frame version 7's word with the CRC computed over
  another version byte (frame version 6's codeword above).
- The four P256-SHA256 vectors of RFC 9382, Appendix B (at least the first,
  A = `server`, B = `client`, and the last, both identities empty): `pA`,
  `pB`, `K`, `Ke`, `KcA`, `KcB`, `cA` and `cB` from the given `w`, `x`, `y`.
- For `0x0123456789abcdef`, computed independently of both sides (Python,
  from the definitions above, after it reproduced RFC 9382's vectors):
  - the fingerprint: `55c5`
  - `w`: `d11cf43f75a28b8c30fd3da84ab44c51c0c8755713eed6dcd9b73907be6af3f7`
  - with `x` the 32 bytes `01..20` and `y` the 32 bytes `21..40`, read big-endian:
    - `pA`: `040dd445d16102c3424f2451006a7aa6e8bfd8a4ccb182905cccf632c27e995012d5f4a0911c67271eb2016e8edc631a1f3633f36ea0f8d67773d127d38697f5a2`
    - `pB`: `0489104b39198d13c02718251d7dc12cee4dc5629b47875240c477cc04600088bcd7cba55f04f2be712ff8b2f971bda93e9323ef61d0e787f89a167b72c8dfd71d`
    - `K`: `044fe243908f378aa1c2a69538822e6ed908c3225d8692575507c649901245150aaa8513bcd739d8100f64e283d32db26502af6ea9bcaa6bb5b58a6bf20f3228b4`
    - `Ke`: `cf4d7f005d568e9b7966f25dbf973474`
    - `cA`: `a716256c3ba4e991e180bb91ed619b2c12fef2882ab978a71609b4321bef47cf`
    - `cB`: `e0bf8b08cb4f684c6ee7ca0f1a82861ed41fbf3ce5babb07b346c048ebcdd551`
    - `S`: `85de6febab32c56e9d93d31f4b364360487cba16d7fde011c8708bb95b45f60a`
- One encrypted `{"op":"status"}` under that `S` with counter 0, and its
  fragments at a 20-byte MTU.

The daemon's test binary prints these vectors with `--print-vectors`, and the
app's `src/protocol/vectors.ts` holds the same bytes; a change to one is a
change to the other.
