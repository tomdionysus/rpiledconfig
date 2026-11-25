# Licensing

RPiLEDConfig, the daemon and the app, is licensed under the Apache License 2.0 (`LICENSE`;
`NOTICE` for the attribution). Everything it is built from keeps its own licence.

## The daemon

| Component | Licence | How it is used |
| --- | --- | --- |
| Boost (Asio only) | Boost Software License 1.0 | Headers only; nothing of Boost is linked |
| OpenSSL 3 (libcrypto) | Apache License 2.0 | Linked dynamically |
| libsystemd (sd-bus) | LGPL-2.1-or-later | Linked dynamically, on Linux |
| GoogleTest | BSD 3-Clause | Test builds only; not in the package |

## The app

The run-time dependencies, as `app/package.json` names them:

| Component | Licence |
| --- | --- |
| react, react-native | MIT |
| react-native-ble-plx | MIT |
| react-native-vision-camera | MIT |
| react-native-worklets-core | MIT |
| react-native-safe-area-context | MIT |
| @noble/hashes, @noble/ciphers, @noble/curves | MIT |

The development dependencies (the TypeScript compiler, ESLint, Prettier, Vitest and the React
Native tooling) are not part of what is shipped. The LED reader's C++ core and its JNI are
this project's own; its tests use GoogleTest (BSD 3-Clause).

A pull request that adds a dependency adds it here, with its licence.
