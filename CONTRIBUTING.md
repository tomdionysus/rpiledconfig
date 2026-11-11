# Contributing

Read this, [README.md](README.md) and [docs/protocol.md](docs/protocol.md) (the
contract between the service and the app) before starting. For anything larger than a
fix, open an issue first and say what you mean to change: the design has decisions
behind it that are not all written in the code.

## Repository layout

```
daemon/    rpiledconfig, the service: C++20, Boost.Asio, OpenSSL, sd-bus; its tests and fuzz targets
app/       RPi LED Config, the Android app: React Native and TypeScript
app/modules/led-reader/   the native LED reader: a C++ core with its own tests, and the VisionCamera plugin
debian/    Debian packaging, built by daemon/scripts/build-deb.sh
docs/      protocol, security, installation, configuration, Google Play, privacy
scripts/   ci.sh, the native half of CI
```

## Branches and CI

- Work happens on `develop`. `main` receives `develop` only at releases, as a merge
  commit (`git merge --no-ff develop`): Codecov files each commit under the first branch
  it was pushed on, so a fast-forward would leave `main` without coverage.
- Tags are `x.y.z`, annotated, with no `v`, on `main`. The daemon's CMake `project()`
  version, `app/app.json`, the Android `versionName` and `versionCode`, and the README's
  version line are the same number; `daemon/packaging/check_version.cmake` and
  `npm run version:check` check it.
- GitHub Actions runs on every push to `main` and `develop` and on pull requests:
  [scripts/ci.sh](scripts/ci.sh) in a Debian trixie container (the format check, the
  daemon's tests plain and under AddressSanitizer and UndefinedBehaviorSanitizer, and
  coverage for the daemon and the LED reader), the app's typecheck, version check and
  tests with coverage, the Debian package built and checked with lintian on arm64, and
  the Android release build. Coverage goes to Codecov under the flags `daemon`, `app` and
  `led-reader`. CodeQL analyses the C++ and the TypeScript.
- CI has no Bluetooth, LED, WiFi or phone. A change to any of those is tested on a Pi and
  a phone as well. `scripts/ci.sh` runs the same on a Pi with Debian trixie.

## Code

- The contract is [docs/protocol.md](docs/protocol.md). A change to what crosses
  Bluetooth or the LED changes that document first, then both sides, then the tests. The
  test vectors are shared: `rpiledconfig --print-vectors` and `app/src/protocol/vectors.ts`
  must agree.
- The daemon: [daemon/.clang-format](daemon/.clang-format) (Google, 160 columns) is authoritative, and
  CI checks it with clang-format 19. `snake_case` functions, variables and files;
  `PascalCase` types; `_prefixed` private members; namespaces `rpiledconfig::<subsystem>`;
  `#pragma once`. Collaborators are injected through constructors and wired in
  `daemon/src/main.cpp`; one `io_context`, strands rather than locks; no exceptions across
  interfaces. Every source file starts with the project's header block; copy it from any
  existing file.
- The app: TypeScript strict, plain React Native. Logic that touches a native module is
  separated from it so Vitest can test it; native modules are stubbed in `app/src/test/`.
- Dependencies are few on purpose. The daemon uses Boost (Asio, headers only), OpenSSL,
  libsystemd and GoogleTest; the app's run-time dependencies are listed in the README. A
  pull request that adds one says why nothing already there will do.
- Everything that reads bytes from Bluetooth is a parser of untrusted input: bounded,
  refusing what it does not understand, and covered by the fuzz targets
  (`-DRPILEDCONFIG_FUZZ=ON`).
- Tests are GoogleTest for the C++ and Vitest for the app, written from
  [docs/protocol.md](docs/protocol.md) rather than from what the code does now. New code
  without tests is not finished.
- No emoji anywhere. No AI or tool attribution in code, comments or commit messages.

## Commits

- One concern per commit. The subject is imperative and under 72 characters; the body
  says why when that is not obvious. No trailers.
- Never commit build output, an upload key or `app/android/keystore.properties`
  ([docs/play-store.md](docs/play-store.md)).

## Documentation

- Docs state current facts: what exists, how to install and use it, what was measured. No
  history and no plans; the README's roadmap is the one place for what comes next.
- No dates, personal names, addresses, hostnames or serials of development hardware in
  `docs/` or the README.

## Security

Report a vulnerability privately, as [SECURITY.md](SECURITY.md) says, not in an issue.
