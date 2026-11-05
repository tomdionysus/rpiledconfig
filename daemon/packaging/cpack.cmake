#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# The Debian package, rpiledconfig, by CPack. Included from CMakeLists.txt after the
# install rules; "cpack -G DEB" in a Release build tree configured with
# CMAKE_INSTALL_PREFIX=/usr makes it. packaging/build-debs.sh at the repository root is
# the reproducible way (milestone 5).
#
# CPack rather than a debian/ directory: the install rules are already here and are the
# only list of what goes where, the version is project()'s with nothing to keep in step,
# and the build image needs dpkg-dev and not debhelper. The maintainer scripts are
# written as dh_installsystemd would write them.

# The unit's ExecStart is generated from the prefix at configure time, so a package from
# any other prefix names the wrong places.
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
    message(STATUS "CPack: CMAKE_INSTALL_PREFIX is ${CMAKE_INSTALL_PREFIX}; configure with -DCMAKE_INSTALL_PREFIX=/usr to build the package")
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "rpiledconfig")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "T. Cully")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Two-factor proximity WiFi setup for a Raspberry Pi")
set(CPACK_PACKAGE_DESCRIPTION "A daemon that draws a one-time 64-bit key when a phone connects, blinks it on the Raspberry Pi's activity LED, and offers a Bluetooth LE service that answers only to a phone that has read it. The phone app proves it knows the key and configures the Pi's WiFi through NetworkManager over an encrypted channel. Line of sight and Bluetooth range are the two factors; nothing is typed and nothing is printed on the board.")

# Absolute destinations (/etc/rpiledconfig) are staged under DESTDIR like the rest,
# rather than written to the build machine's /etc.
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

# The Debian revision carries the suite, because the package is built once for each:
# OpenSSL's package name differs between bookworm (libssl3) and trixie (libssl3t64).
# "~deb12" sorts before "~deb13", so a dist-upgrade moves to the trixie build.
set(RPILEDCONFIG_DEB_RELEASE "1" CACHE STRING "The Debian revision, 1~deb12 say")
set(RPILEDCONFIG_DEB_MAINTAINER "T. Cully <tomhughcully@gmail.com>" CACHE STRING "The package's Maintainer field")

set(CPACK_DEBIAN_PACKAGE_NAME "rpiledconfig")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${RPILEDCONFIG_DEB_RELEASE}")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${RPILEDCONFIG_DEB_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# The libraries' packages come from dpkg-shlibdeps, which reads the binary: libssl3 (or
# libssl3t64), libsystemd0, libstdc++6 and libc6, each at the soname and version the build
# suite has. Boost is headers only and appears in none of it. adduser for the account and
# init-system-helpers for deb-systemd-helper are what the maintainer scripts call; bluez
# and network-manager are what the daemon talks to; polkitd is what reads the rule that
# lets the service user change WiFi (without polkitd running, NetworkManager refuses every
# change from anyone but root). polkitd is the package on both bookworm (122) and trixie.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "adduser, init-system-helpers (>= 1.54~), bluez, network-manager, polkitd")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "systemd")

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/conffiles"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm"
)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

include(CPack)
