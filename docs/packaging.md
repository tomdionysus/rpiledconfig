# Packaging

The daemon ships as a Debian package, `rpiledconfig`, for arm64: 64-bit Raspberry Pi OS
(trixie) is the reference host. `debian/` holds the packaging, built with debhelper; the files
it installs are in `daemon/packaging/`. `docs/installation.md` says what it installs and how.

## Building the package

On a Pi running 64-bit Raspberry Pi OS (trixie), or any Debian trixie arm64 machine, from a
git clone of this repository:

    sudo apt install dpkg-dev && sudo apt build-dep ./
    tools/build-deb.sh

The packages land in `build-deb/`: `rpiledconfig_<version>_arm64.deb` and
`rpiledconfig-dbgsym_<version>_arm64.deb`, which holds the debug symbols. The build runs the
daemon's tests; `DEB_BUILD_OPTIONS=nocheck` skips them. CI builds the package the same way on
arm64 and fails it on any lintian error or warning:

    lintian --fail-on error,warning build-deb/*.changes

## The version

The version comes from `git describe`. A tag gives its own, such as `0.5.2`; a build three
commits after it gives `0.5.2+git3.gabc1234`, which sorts after `0.5.2` and before the next
release; uncommitted changes add `.dirty`. A build of a tag must agree with the daemon's CMake
`project()` version, and that with `app/app.json` and the README's version line, or the build
stops.

`apt` will not reinstall a package of the version already installed, so a rebuild of the same
version is installed with `sudo dpkg -i --force-confold build-deb/rpiledconfig_<version>_arm64.deb`.
