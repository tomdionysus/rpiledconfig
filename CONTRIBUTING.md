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
debian/    Debian packaging, built by tools/build-deb.sh
docs/      protocol, security, installation, configuration, building, packaging, development,
           Google Play, privacy, licensing
tools/     ci.sh (one CI step), build-deb.sh, linux-tests.sh
```

## Building and testing

The daemon, on Debian trixie (docs/building.md has the packages, the Mac, and the rest):

```
cmake -S daemon -B daemon/build -DCMAKE_BUILD_TYPE=Release -DRPILEDCONFIG_BUILD_TESTING=ON -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
cmake --build daemon/build -j8
dbus-run-session -- sh -c 'RPILEDCONFIG_TEST_DBUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS exec daemon/build/rpiledconfig_tests'
```

The LED reader:

```
cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -DRPILEDCONFIG_WARNINGS_AS_ERRORS=ON
cmake --build app/modules/led-reader/cpp/build -j8
app/modules/led-reader/cpp/build/led_reader_tests
```

The app: `cd app && npm install && npm run typecheck && npm run lint && npm test`.

## Branches and CI

- Work happens on `develop`. `main` receives `develop` only at releases, as a merge
  commit (`git merge --no-ff develop`): Codecov files each commit under the first branch
  it was pushed on, so a fast-forward would leave `main` without coverage.
- Tags are `x.y.z`, annotated, with no `v`, on `main`. The daemon's CMake `project()`
  version, `app/app.json`, the Android `versionName` and `versionCode`, and the README's
  version line are the same number; `daemon/packaging/check_version.cmake` and
  `npm run version:check` check it.
- GitHub Actions runs on every push to `main` and `develop` and on pull requests: each step
  of [tools/ci.sh](tools/ci.sh) as its own job in a Debian trixie container (the header and
  format check; release builds with warnings as errors on x86-64 and arm64; clang; ASan with
  UBSan; TSan; the fuzz targets; coverage), the Debian package built and checked with
  lintian on arm64, a build and test on macOS, the Android release build, and the app's
  typecheck, lint, format check, version check and tests with coverage. Coverage goes to
  Codecov under the flags `daemon`, `app` and `led-reader`. CodeQL analyses the C++ and the
  TypeScript. docs/development.md has the steps.
- CI has no Bluetooth, LED, WiFi or phone. A change to any of those is tested on a Pi and
  a phone as well. `tools/ci.sh <step>` runs the same on any Debian trixie machine, a Pi
  included.

## Code

- The contract is [docs/protocol.md](docs/protocol.md). A change to what crosses
  Bluetooth or the LED changes that document first, then both sides, then the tests. The
  test vectors are shared: `rpiledconfig --print-vectors` and `app/src/protocol/vectors.ts`
  must agree.
- The C++, the daemon's and the LED reader's: [.clang-format](.clang-format) (Google, 160
  columns) is authoritative, and CI checks it with clang-format 19. The warnings are
  `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (and `-Wthread-safety` under clang), as
  errors in CI. `snake_case` functions, variables and files;
  `PascalCase` types; `_prefixed` private members; namespaces `rpiledconfig::<subsystem>`;
  `#pragma once`. Collaborators are injected through constructors and wired in
  `daemon/src/main.cpp`; one `io_context`, strands rather than locks; no exceptions across
  interfaces.
- Every source file starts with the project's header block (C++, TypeScript, JavaScript,
  Kotlin, Gradle, CMake, shell and YAML alike); copy it from any existing file of the same
  kind. `tools/ci.sh check` checks it.
- The app: TypeScript strict, plain React Native. `npm run lint` (ESLint, with the type
  checker and React's rules of hooks) and `npm run format:check` (Prettier) must pass;
  `npm run format` applies Prettier. Logic that touches a native module is separated from it
  so Vitest can test it; native modules are stubbed in `app/src/test/`.
- Dependencies are few on purpose. The daemon uses Boost (Asio, headers only), OpenSSL,
  libsystemd and GoogleTest; the app's run-time dependencies are listed with their licences
  in [docs/licensing.md](docs/licensing.md). A pull request that adds one says why nothing
  already there will do, and adds it there.
- Everything that reads bytes from Bluetooth is a parser of untrusted input: bounded,
  refusing what it does not understand, and covered by the fuzz targets
  (`-DRPILEDCONFIG_BUILD_FUZZING=ON`).
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
