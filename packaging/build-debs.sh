#!/bin/sh
#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# Build the rpiledconfig .deb files in Docker, for each Debian suite and architecture,
# into dist/ at the repository root.
#
#   packaging/build-debs.sh                         bookworm and trixie, arm64 and amd64
#   packaging/build-debs.sh --suite bookworm --arch arm64
#   packaging/build-debs.sh --tests                 also build and run the daemon's tests
#
# This tree is taken as it is, less what .dockerignore leaves out. Build a release from a
# clean checkout of its tag.
#
# Needs Docker with BuildKit (docker buildx). A foreign architecture runs under QEMU: Docker
# Desktop has it; on a Linux host, once: docker run --privileged --rm tonistiigi/binfmt --install arm64,amd64

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
suites="bookworm trixie"
arches="arm64 amd64"
run_tests=0

usage() {
    echo "usage: packaging/build-debs.sh [--suite bookworm|trixie|all] [--arch arm64|amd64|all] [--tests]"
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --suite) suites=$2; shift 2 ;;
        --arch) arches=$2; shift 2 ;;
        --tests) run_tests=1; shift ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 2 ;;
    esac
done

[ "$suites" = all ] && suites="bookworm trixie"
[ "$arches" = all ] && arches="arm64 amd64"

commit=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ -n "$(git -C "$root" status --porcelain 2>/dev/null)" ]; then
    commit="$commit (with uncommitted changes)"
fi

# Timestamps inside the build come from the last commit, not from the clock.
SOURCE_DATE_EPOCH=$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)
export SOURCE_DATE_EPOCH

echo "rpiledconfig $commit"

mkdir -p "$root/dist"

for suite in $suites; do
    for arch in $arches; do
        echo "--- $suite $arch"
        docker buildx build \
            --file "$root/packaging/Dockerfile" \
            --platform "linux/$arch" \
            --build-arg "DEBIAN_SUITE=$suite" \
            --build-arg "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH" \
            --build-arg "RPILEDCONFIG_RUN_TESTS=$run_tests" \
            --target export \
            --output "type=local,dest=$root/dist" \
            "$root"
    done
done

ls -l "$root"/dist/*.deb
