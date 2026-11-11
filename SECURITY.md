# Security

RPiLEDConfig answers to anyone in Bluetooth range of a Pi, so its security matters
more than most of what it does. [docs/security.md](docs/security.md) says what the
service exposes, what an attacker in range can and cannot do, and the bounds that are
accepted.

## Supported versions

Only the latest release gets security fixes. The service and the app are released
together under one version number.

## Reporting a vulnerability

Do not open a public issue. Report it privately, either through GitHub's
[private vulnerability reporting](https://github.com/tomdionysus/rpiledconfig/security/advisories/new)
or by email to mail@tomcully.com.

Say which version and which side (the service, the app, or the protocol), what an
attacker needs (range, sight of the LED, a paired phone, a local account on the Pi),
and what they get. A reproduction or a proof of concept helps.

You will get an answer within a week. A confirmed vulnerability is fixed in a release
before it is described in public, and the report credits you unless you ask it not to.

## In scope

- The protocol in [docs/protocol.md](docs/protocol.md): the key, SPAKE2, the session
  encryption, the rate limit.
- Anything the service parses from Bluetooth, and what it does with a request.
- The service's privileges: the systemd unit, the polkit and udev rules, the uninstall
  unit.
- The app's handling of what a Pi sends it.

## Out of scope

- An attacker who already has root on the Pi or control of the phone.
- The bounds [docs/security.md](docs/security.md) accepts, unless the report shows they
  are weaker than it says.
- Denial of service by jamming the radio.
