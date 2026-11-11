#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
cmake_minimum_required(VERSION 3.23...4.4)

project(rpiledconfig VERSION 0.5.0 LANGUAGES CXX)

option(RPILEDCONFIG_BUILD_TESTING "Build test targets" OFF)
option(RPILEDCONFIG_FUZZ "Build the fuzzers in tests/fuzz: clang only" OFF)
if(RPILEDCONFIG_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

set(RPILEDCONFIG_SANITIZE "none" CACHE STRING "Sanitizer to build with: none, address, thread")
set_property(CACHE RPILEDCONFIG_SANITIZE PROPERTY STRINGS none address thread)

# Single-config generators leave CMAKE_BUILD_TYPE empty, which means no optimisation
# and no debug info. The package passes RelWithDebInfo (debian/rules).
get_property(RPILEDCONFIG_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(NOT RPILEDCONFIG_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clangd reads compile_commands.json from the source tree, and without it an editor
# parses these files as whatever C++ it defaults to rather than C++20. The copy is made
# at configure time and is ignored by git.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(RPILEDCONFIG_SANITIZE STREQUAL "address")
    set(RPILEDCONFIG_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
elseif(RPILEDCONFIG_SANITIZE STREQUAL "thread")
    set(RPILEDCONFIG_SANITIZE_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
elseif(NOT RPILEDCONFIG_SANITIZE STREQUAL "none")
    message(FATAL_ERROR "RPILEDCONFIG_SANITIZE must be one of: none, address, thread")
endif()

# What the daemon's own code is held to. Not -Werror: a new compiler's new warning
# should not stop somebody building a release, and the presets are where "no warnings"
# is checked.
set(RPILEDCONFIG_WARNINGS -Wall -Wextra)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/build_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/build_version.h"
    @ONLY
)

function(rpiledconfig_add_prefix prefix)
    if(prefix AND IS_DIRECTORY "${prefix}")
        list(PREPEND CMAKE_PREFIX_PATH "${prefix}")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)

        if(IS_DIRECTORY "${prefix}/lib/pkgconfig")
            list(PREPEND RPILEDCONFIG_PKG_CONFIG_PATHS "${prefix}/lib/pkgconfig")
            set(RPILEDCONFIG_PKG_CONFIG_PATHS "${RPILEDCONFIG_PKG_CONFIG_PATHS}" PARENT_SCOPE)
        endif()

        if(IS_DIRECTORY "${prefix}/share/pkgconfig")
            list(PREPEND RPILEDCONFIG_PKG_CONFIG_PATHS "${prefix}/share/pkgconfig")
            set(RPILEDCONFIG_PKG_CONFIG_PATHS "${RPILEDCONFIG_PKG_CONFIG_PATHS}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# The development Mac takes its libraries from MacPorts under /opt/local. MacPorts'
# plain "boost" port lags (1.76, older than the 1.80 Asio needed here), so the newest
# versioned boostNNN port under libexec/boost is preferred; it is added last because
# rpiledconfig_add_prefix prepends.
if(APPLE)
    rpiledconfig_add_prefix("/opt/local")

    file(GLOB RPILEDCONFIG_MACPORTS_BOOSTS LIST_DIRECTORIES true "/opt/local/libexec/boost/*")

    if(RPILEDCONFIG_MACPORTS_BOOSTS)
        list(SORT RPILEDCONFIG_MACPORTS_BOOSTS COMPARE NATURAL ORDER DESCENDING)
        list(GET RPILEDCONFIG_MACPORTS_BOOSTS 0 RPILEDCONFIG_MACPORTS_BOOST)
        rpiledconfig_add_prefix("${RPILEDCONFIG_MACPORTS_BOOST}")
    endif()

    if(CMAKE_PREFIX_PATH)
        list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
    endif()

    if(RPILEDCONFIG_PKG_CONFIG_PATHS)
        list(REMOVE_DUPLICATES RPILEDCONFIG_PKG_CONFIG_PATHS)
        list(JOIN RPILEDCONFIG_PKG_CONFIG_PATHS ":" RPILEDCONFIG_PKG_CONFIG_PATH)

        if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
            set(ENV{PKG_CONFIG_PATH} "${RPILEDCONFIG_PKG_CONFIG_PATH}:$ENV{PKG_CONFIG_PATH}")
        else()
            set(ENV{PKG_CONFIG_PATH} "${RPILEDCONFIG_PKG_CONFIG_PATH}")
        endif()
    endif()
endif()

# Boost is Asio and nothing else, which is headers only: no component is asked for and
# nothing of Boost is linked, so the package depends on no libboost.
find_package(Boost 1.80.0 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

if(TARGET Boost::headers)
    set(RPILEDCONFIG_BOOST_TARGET Boost::headers)
elseif(TARGET Boost::boost)
    set(RPILEDCONFIG_BOOST_TARGET Boost::boost)
else()
    message(FATAL_ERROR "Boost was found, but no header-only target was provided")
endif()

# BlueZ and NetworkManager are reached over D-Bus through libsystemd's sd-bus, which
# CLAUDE.md allows. Without it (the development Mac, or a Linux without systemd) the
# bluez and networkmanager drivers are not built, and a configuration that asks for them
# is refused at startup; the fakes are always there.
set(RPILEDCONFIG_HAVE_SD_BUS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(RPILEDCONFIG_SYSTEMD IMPORTED_TARGET libsystemd)
    endif()

    if(RPILEDCONFIG_SYSTEMD_FOUND)
        set(RPILEDCONFIG_HAVE_SD_BUS ON)
        message(STATUS "BLE and WiFi: BlueZ and NetworkManager over sd-bus (libsystemd ${RPILEDCONFIG_SYSTEMD_VERSION})")
    else()
        message(WARNING "libsystemd was not found: the bluez and networkmanager drivers are not built, only the fakes")
    endif()
endif()

# --- The daemon --------------------------------------------------------------------------

file(GLOB_RECURSE RPILEDCONFIG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

list(REMOVE_ITEM RPILEDCONFIG_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

# The sd-bus drivers are whole files behind RPILEDCONFIG_HAVE_SD_BUS; left out of a build
# without it rather than compiled to nothing.
if(NOT RPILEDCONFIG_HAVE_SD_BUS)
    list(REMOVE_ITEM RPILEDCONFIG_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ble/ble_bluez.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wifi/wifi_networkmanager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/system/uninstaller_systemd.cpp"
    )
    list(FILTER RPILEDCONFIG_SOURCES EXCLUDE REGEX "/src/dbus/")
endif()

# Wi-Fi Easy Connect through wpa_supplicant binds its client sockets in Linux's abstract
# namespace; elsewhere only the fake is built.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(REMOVE_ITEM RPILEDCONFIG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/wifi/easy_connect_wpa_supplicant.cpp")
endif()

add_library(rpiledconfig_core STATIC ${RPILEDCONFIG_SOURCES})

target_compile_features(rpiledconfig_core PUBLIC cxx_std_20)
target_compile_options(rpiledconfig_core PRIVATE ${RPILEDCONFIG_WARNINGS})

target_include_directories(rpiledconfig_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)

if(RPILEDCONFIG_SANITIZE_FLAGS)
    target_compile_options(rpiledconfig_core PUBLIC ${RPILEDCONFIG_SANITIZE_FLAGS})
    target_link_options(rpiledconfig_core PUBLIC ${RPILEDCONFIG_SANITIZE_FLAGS})
endif()

# This program parses what strangers send it over a radio. Whatever gets past the code should find
# as little to work with as the toolchain can leave it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT RPILEDCONFIG_FUZZ)
    target_compile_options(rpiledconfig_core PUBLIC
        -fstack-protector-strong
        -fstack-clash-protection
        -D_GLIBCXX_ASSERTIONS
        # A distribution's build flags may set a level of their own first (dpkg-buildflags does).
        $<$<CONFIG:Release,RelWithDebInfo>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        target_compile_options(rpiledconfig_core PUBLIC -mbranch-protection=standard)
    endif()
    target_link_options(rpiledconfig_core PUBLIC -Wl,-z,relro,-z,now,-z,noexecstack)
endif()

target_link_libraries(rpiledconfig_core
    PUBLIC
        "${RPILEDCONFIG_BOOST_TARGET}"
        OpenSSL::Crypto
        Threads::Threads
)

if(RPILEDCONFIG_HAVE_SD_BUS)
    target_link_libraries(rpiledconfig_core PUBLIC PkgConfig::RPILEDCONFIG_SYSTEMD)
    target_compile_definitions(rpiledconfig_core PUBLIC RPILEDCONFIG_HAVE_SD_BUS=1)
endif()

add_executable(rpiledconfig "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

target_link_libraries(rpiledconfig PRIVATE rpiledconfig_core)
target_compile_features(rpiledconfig PRIVATE cxx_std_20)
target_compile_options(rpiledconfig PRIVATE ${RPILEDCONFIG_WARNINGS})

# --- Install layout --------------------------------------------------------------------
#
# Where everything goes, worked out before the tests because one of them parses the
# configuration generated here. The package is built with CMAKE_INSTALL_PREFIX=/usr; the
# same rules serve "sudo cmake --install" into /usr/local, and a staging prefix anywhere.

set(CMAKE_INSTALL_DOCDIR "share/doc/rpiledconfig" CACHE PATH "Documentation (share/doc/rpiledconfig)")
include(GNUInstallDirs)

set(RPILEDCONFIG_SHARE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/rpiledconfig")
set(RPILEDCONFIG_SHARE_FULL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/rpiledconfig")

# /etc and /var/lib for a system prefix, under the prefix otherwise, so a staging install
# into a scratch directory stays inside it.
if(CMAKE_INSTALL_PREFIX STREQUAL "/usr" OR CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    set(RPILEDCONFIG_SYSTEM_PREFIX ON)
else()
    set(RPILEDCONFIG_SYSTEM_PREFIX OFF)
endif()

if(NOT DEFINED RPILEDCONFIG_CONFIG_INSTALL_DIR)
    if(RPILEDCONFIG_SYSTEM_PREFIX)
        set(RPILEDCONFIG_CONFIG_INSTALL_DIR "/etc/rpiledconfig")
    else()
        set(RPILEDCONFIG_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/etc/rpiledconfig")
    endif()
endif()

# The unit and the udev rule under the prefix: /usr/lib/systemd/system and
# /usr/lib/udev/rules.d for the package, where bookworm and trixie both look, and
# /usr/local/... for a local install. There is no D-Bus policy: the distributions' own
# allow everything the daemon does (docs/installation.md).
set(RPILEDCONFIG_SYSTEMD_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system" CACHE PATH "Where the systemd unit is installed")
set(RPILEDCONFIG_UDEV_RULES_DIR "${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d" CACHE PATH "Where the udev rule is installed")

# The polkit rule NetworkManager needs before the service user may change WiFi. polkitd
# reads /usr/share/polkit-1/rules.d and /etc/polkit-1/rules.d only, so a /usr/local install
# puts it where polkitd will not see it: copy it to /etc/polkit-1/rules.d by hand then
# (docs/installation.md).
set(RPILEDCONFIG_POLKIT_RULES_DIR "${CMAKE_INSTALL_PREFIX}/share/polkit-1/rules.d" CACHE PATH "Where the polkit rule is installed")

# The unit, the udev rule and the polkit rule belong to a Linux host. On by default
# there; turning it on elsewhere lets the install tree be inspected on a development Mac.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RPILEDCONFIG_INSTALL_SYSTEM_FILES_DEFAULT ON)
else()
    set(RPILEDCONFIG_INSTALL_SYSTEM_FILES_DEFAULT OFF)
endif()
option(RPILEDCONFIG_INSTALL_SYSTEM_FILES "Install the systemd unit, the udev rule and the polkit rule" ${RPILEDCONFIG_INSTALL_SYSTEM_FILES_DEFAULT})

configure_file(packaging/rpiledconfig.conf.in ${CMAKE_CURRENT_BINARY_DIR}/packaging/rpiledconfig.conf @ONLY)
configure_file(packaging/rpiledconfig.service.in ${CMAKE_CURRENT_BINARY_DIR}/packaging/rpiledconfig.service @ONLY)

if(RPILEDCONFIG_BUILD_TESTING)
    include(CTest)
    enable_testing()

    find_package(GTest REQUIRED)

    file(GLOB_RECURSE RPILEDCONFIG_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )

    # The D-Bus tests (mock BlueZ and NetworkManager on a private bus, and the drivers
    # against them) need sd-bus to build at all. Built, they still skip at run time unless
    # RPILEDCONFIG_TEST_DBUS_ADDRESS names a bus: scripts/linux-tests.sh starts one.
    if(NOT RPILEDCONFIG_HAVE_SD_BUS)
        list(FILTER RPILEDCONFIG_TEST_SOURCES EXCLUDE REGEX "/tests/dbus/")
    endif()

    # The wpa_supplicant enrollee against a fake wpa_supplicant on a socket in a temporary
    # directory: Linux only, as the enrollee is, and needing no bus.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(FILTER RPILEDCONFIG_TEST_SOURCES EXCLUDE REGEX "/tests/wifi/easy_connect_wpa_supplicant_test.cpp$")
    endif()

    list(FILTER RPILEDCONFIG_TEST_SOURCES EXCLUDE REGEX "/tests/fuzz/")

    add_executable(rpiledconfig_tests ${RPILEDCONFIG_TEST_SOURCES})

    target_link_libraries(rpiledconfig_tests PRIVATE rpiledconfig_core)
    target_include_directories(rpiledconfig_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_options(rpiledconfig_tests PRIVATE ${RPILEDCONFIG_WARNINGS})

    if(TARGET GTest::gtest_main)
        target_link_libraries(rpiledconfig_tests PRIVATE GTest::gtest_main)
    elseif(TARGET GTest::Main)
        target_link_libraries(rpiledconfig_tests PRIVATE GTest::Main)
    else()
        message(FATAL_ERROR "GTest was found, but no usable main target was provided")
    endif()

    target_compile_features(rpiledconfig_tests PRIVATE cxx_std_20)

    # The example configuration is a test fixture too: it has to parse, and the working
    # directory a test runs in is the build tree rather than the source one.
    target_compile_definitions(rpiledconfig_tests PRIVATE RPILEDCONFIG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # So is the configuration a package installs, as generated for this build's prefix.
    target_compile_definitions(rpiledconfig_tests PRIVATE
        RPILEDCONFIG_TEST_PACKAGED_CONFIG="${CMAKE_CURRENT_BINARY_DIR}/packaging/rpiledconfig.conf")

    include(GoogleTest)
    gtest_discover_tests(rpiledconfig_tests DISCOVERY_TIMEOUT 60)

    # The package's version is this project()'s; the app and the README must agree.
    add_test(NAME version_agrees
             COMMAND ${CMAKE_COMMAND}
                     -DRPILEDCONFIG_VERSION=${PROJECT_VERSION}
                     -DRPILEDCONFIG_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/check_version.cmake)
endif()

# --- Install ---------------------------------------------------------------------------
#
# The binary, the unit, the udev rule, the D-Bus policy, the documentation, and a
# configuration that is never overwritten once it exists: an upgrade that replaced what
# an operator configured would put a Pi back into setup mode with the wrong LED. (The
# package marks it a conffile, and dpkg keeps it; the install(CODE) below does the same
# for "cmake --install".)

install(TARGETS rpiledconfig RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES config/rpiledconfig.example.conf DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES
            ../README.md
            ../LICENSE
            ../NOTICE
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
file(GLOB RPILEDCONFIG_DOCS "${CMAKE_CURRENT_SOURCE_DIR}/../docs/*.md")
install(FILES ${RPILEDCONFIG_DOCS} DESTINATION ${CMAKE_INSTALL_DOCDIR})

# DESTDIR is honoured by hand, because install(CODE) writes the paths it is given and
# nothing prepends it for us. A packager staging a build must not have the real
# /etc/rpiledconfig written.
install(CODE "
    set(RPILEDCONFIG_CONFIG_DIR \"${RPILEDCONFIG_CONFIG_INSTALL_DIR}\")

    if(NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
        set(RPILEDCONFIG_CONFIG_DIR \"\$ENV{DESTDIR}/\${RPILEDCONFIG_CONFIG_DIR}\")
    endif()

    if(EXISTS \"\${RPILEDCONFIG_CONFIG_DIR}/rpiledconfig.conf\")
        message(STATUS \"Keeping existing \${RPILEDCONFIG_CONFIG_DIR}/rpiledconfig.conf\")
    else()
        message(STATUS \"Installing \${RPILEDCONFIG_CONFIG_DIR}/rpiledconfig.conf\")
        file(MAKE_DIRECTORY \"\${RPILEDCONFIG_CONFIG_DIR}\")
        file(COPY_FILE \"${CMAKE_CURRENT_BINARY_DIR}/packaging/rpiledconfig.conf\"
                       \"\${RPILEDCONFIG_CONFIG_DIR}/rpiledconfig.conf\")
    endif()
")

if(RPILEDCONFIG_INSTALL_SYSTEM_FILES)
    # Generated rather than copied, so ExecStart names where the binary actually went.
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/packaging/rpiledconfig.service DESTINATION ${RPILEDCONFIG_SYSTEMD_UNIT_DIR})
    install(FILES packaging/rpiledconfig-uninstall.service DESTINATION ${RPILEDCONFIG_SYSTEMD_UNIT_DIR})
    install(FILES packaging/60-rpiledconfig.rules DESTINATION ${RPILEDCONFIG_UDEV_RULES_DIR})
    install(FILES packaging/polkit-rpiledconfig.rules DESTINATION ${RPILEDCONFIG_POLKIT_RULES_DIR} RENAME 60-rpiledconfig.rules)
endif()

# --- Editor support ---------------------------------------------------------------------

# A symlink rather than a copy, so a rebuild of whichever preset was configured last is
# what the editor sees. file(CREATE_LINK) falls back to a copy where links are refused.
file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json" COPY_ON_ERROR SYMBOLIC)

if(RPILEDCONFIG_FUZZ)
    foreach(fuzzer fuzz_attacker fuzz_parsers)
        add_executable(${fuzzer} tests/fuzz/${fuzzer}.cpp)
        target_link_libraries(${fuzzer} PRIVATE rpiledconfig_core)
        target_include_directories(${fuzzer} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        target_compile_features(${fuzzer} PRIVATE cxx_std_20)
        target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer)
    endforeach()
endif()
