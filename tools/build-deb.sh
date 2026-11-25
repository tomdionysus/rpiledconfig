#!/bin/sh
#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# Builds the Debian package, rpiledconfig, and its debug symbols, on a Raspberry Pi running
# 64-bit Raspberry Pi OS (trixie), from a git checkout. The packages are written to
# build-deb/ at the repository root.
#
#   sudo apt install dpkg-dev && sudo apt build-dep ./
#   daemon/scripts/build-deb.sh [dpkg-buildpackage options]
#
# The version comes from `git describe`: the tag 0.5.0 gives 0.5.0, and a build three
# commits later 0.5.0+git3.gabc1234, which sorts after 0.5.0 and before 0.5.1. Uncommitted
# changes add .dirty. DEB_BUILD_OPTIONS=nocheck skips the tests.

set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"

describe=$(git describe --tags --match '[0-9]*.[0-9]*.[0-9]*' --dirty)
version=$(echo "$describe" | sed -E 's/-([0-9]+)-g([0-9a-f]+)/+git\1.g\2/; s/-dirty$/.dirty/')

# A build of a tag is a release, and has to be the version the tree says. Between tags the
# tree may already carry the next one.
project=$(sed -nE 's/^project\(rpiledconfig VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' daemon/CMakeLists.txt)
case "$version" in
    *+git*) ;;
    "$project" | "$project".dirty) ;;
    *)
        echo "build-deb.sh: the tag says $describe and daemon/CMakeLists.txt says $project" >&2
        exit 1
        ;;
esac
cmake -P daemon/packaging/check_version.cmake

cat >debian/changelog <<END
rpiledconfig ($version) trixie; urgency=medium

  * Built from $describe.

 -- Tom Cully <mail@tomcully.com>  $(date -R)
END

dpkg-buildpackage -us -uc -b "$@"

mkdir -p build-deb
for f in ../rpiledconfig*_"$version"_*.deb ../rpiledconfig_"$version"_*.buildinfo ../rpiledconfig_"$version"_*.changes; do
    if [ -e "$f" ]; then mv "$f" build-deb/; fi
done
ls -l build-deb/*"$version"*
