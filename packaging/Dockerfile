# syntax=docker/dockerfile:1.7
#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# The rpiledconfig Debian package, built from this tree for one Debian suite and one
# architecture per run. packaging/build-debs.sh runs it for each; by hand:
#
#   docker buildx build -f packaging/Dockerfile \
#       --platform linux/arm64 --build-arg DEBIAN_SUITE=bookworm \
#       --target export --output type=local,dest=dist .
#
# The context is the repository root. Nothing built on the host goes in.
#
# Stages: a toolchain for the target suite; the daemon and its package; and a scratch
# stage holding only the .deb, for --output. Boost is used as headers only (Asio), so the
# distribution's libboost-dev is enough and nothing is built from source.

ARG DEBIAN_SUITE=bookworm

# --- The toolchain for the target suite -------------------------------------------------

FROM debian:${DEBIAN_SUITE}-slim AS toolchain

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        dpkg-dev \
        file \
        libboost-dev \
        libgtest-dev \
        libssl-dev \
        libsystemd-dev \
        ninja-build \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# --- The daemon and its package ---------------------------------------------------------

FROM toolchain AS package

ARG DEBIAN_SUITE
ARG SOURCE_DATE_EPOCH
# 1 builds the tests preset too and runs it before packaging. Slow under emulation, so off
# unless asked for.
ARG RPILEDCONFIG_RUN_TESTS=0

WORKDIR /src/rpiledconfig
COPY README.md LICENSE NOTICE ./
COPY docs ./docs
COPY app/package.json app/app.json ./app/
COPY daemon ./daemon

RUN cmake -P daemon/packaging/check_version.cmake

RUN set -eux; \
    case "${DEBIAN_SUITE}" in \
        bookworm) release="1~deb12" ;; \
        trixie)   release="1~deb13" ;; \
        *)        release="1~${DEBIAN_SUITE}" ;; \
    esac; \
    if [ "${RPILEDCONFIG_RUN_TESTS}" = "1" ]; then \
        cmake -S daemon -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRPILEDCONFIG_BUILD_TESTING=ON; \
        cmake --build build-tests -j"$(nproc)"; \
        ./build-tests/rpiledconfig_tests; \
    fi; \
    cmake -S daemon -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DRPILEDCONFIG_DEB_RELEASE="${release}"; \
    cmake --build build -j"$(nproc)"; \
    (cd build && cpack -G DEB); \
    mkdir -p /out; \
    cp build/*.deb /out/

# What the package must be, checked here rather than found out on a Pi: the libraries it
# depends on, and the files where the unit and the docs say they are.
RUN set -eux; \
    deb="$(ls /out/*.deb)"; \
    depends="$(dpkg-deb -f "${deb}" Depends)"; \
    echo "Depends: ${depends}"; \
    for lib in libssl3 libsystemd0 adduser; do \
        echo "${depends}" | grep -q "${lib}" || { echo "missing dependency: ${lib}"; exit 1; }; \
    done; \
    if echo "${depends}" | grep -q boost; then echo "depends on a Boost package"; exit 1; fi; \
    rm -rf /tmp/unpacked; dpkg-deb -x "${deb}" /tmp/unpacked; \
    for path in \
        usr/bin/rpiledconfig \
        usr/lib/systemd/system/rpiledconfig.service \
        usr/lib/udev/rules.d/60-rpiledconfig.rules \
        usr/share/polkit-1/rules.d/60-rpiledconfig.rules \
        usr/share/doc/rpiledconfig/copyright \
        usr/share/doc/rpiledconfig/installation.md \
        usr/share/doc/rpiledconfig/protocol.md \
        etc/rpiledconfig/rpiledconfig.conf; do \
        test -e "/tmp/unpacked/${path}" || { echo "missing from the package: /${path}"; exit 1; }; \
    done; \
    dpkg-deb -e "${deb}" /tmp/control; \
    grep -qx /etc/rpiledconfig/rpiledconfig.conf /tmp/control/conffiles; \
    /tmp/unpacked/usr/bin/rpiledconfig --version; \
    dpkg-deb -I "${deb}"

# --- The output --------------------------------------------------------------------------

FROM scratch AS export
COPY --from=package /out/ /
