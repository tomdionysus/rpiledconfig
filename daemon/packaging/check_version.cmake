#
# RPiLEDConfig - daemon
#
# Copyright (C) 2026 T. Cully
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# One version for the whole tree. The daemon's project() version is the package's; the
# app's app.json and the README's version line must say the same, or a package would ship
# a README, and the store an app, that name another release.
#
#   cmake -DRPILEDCONFIG_VERSION=0.1.0 -DRPILEDCONFIG_REPO_ROOT=/path/to/rpiledconfig -P check_version.cmake
#
# Without RPILEDCONFIG_VERSION the version is read from daemon/CMakeLists.txt, so the
# script runs on its own too. Registered as the CTest test version_agrees, and run by the
# package build before anything is compiled.

cmake_minimum_required(VERSION 3.19)

if(NOT RPILEDCONFIG_REPO_ROOT)
    get_filename_component(RPILEDCONFIG_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

if(NOT RPILEDCONFIG_VERSION)
    file(READ "${RPILEDCONFIG_REPO_ROOT}/daemon/CMakeLists.txt" RPILEDCONFIG_CMAKELISTS)
    if(NOT RPILEDCONFIG_CMAKELISTS MATCHES "project\\(rpiledconfig VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)")
        message(FATAL_ERROR "No project(rpiledconfig VERSION x.y.z) in daemon/CMakeLists.txt")
    endif()
    set(RPILEDCONFIG_VERSION "${CMAKE_MATCH_1}")
endif()

set(RPILEDCONFIG_VERSION_ERRORS "")

function(rpiledconfig_expect what found)
    if(NOT found STREQUAL RPILEDCONFIG_VERSION)
        set(RPILEDCONFIG_VERSION_ERRORS "${RPILEDCONFIG_VERSION_ERRORS}\n  ${what} says \"${found}\"" PARENT_SCOPE)
    endif()
endfunction()

# Expo keeps the version under "expo" in app.json; a plain top-level "version" is taken
# too, for an app.json that is not Expo's shape.
if(EXISTS "${RPILEDCONFIG_REPO_ROOT}/app/app.json")
    file(READ "${RPILEDCONFIG_REPO_ROOT}/app/app.json" RPILEDCONFIG_APP_JSON)
    string(JSON RPILEDCONFIG_APP_VERSION ERROR_VARIABLE RPILEDCONFIG_JSON_ERROR GET "${RPILEDCONFIG_APP_JSON}" expo version)
    if(RPILEDCONFIG_JSON_ERROR)
        string(JSON RPILEDCONFIG_APP_VERSION ERROR_VARIABLE RPILEDCONFIG_JSON_ERROR GET "${RPILEDCONFIG_APP_JSON}" version)
    endif()
    if(RPILEDCONFIG_JSON_ERROR)
        set(RPILEDCONFIG_APP_VERSION "(unreadable: ${RPILEDCONFIG_JSON_ERROR})")
    endif()
    rpiledconfig_expect("app/app.json" "${RPILEDCONFIG_APP_VERSION}")
else()
    rpiledconfig_expect("app/app.json" "(no such file)")
endif()

# The README's version line is its first line in italics: "*v0.1.0 (in development)*".
file(STRINGS "${RPILEDCONFIG_REPO_ROOT}/README.md" RPILEDCONFIG_README_LINE REGEX "^\\*v[0-9]+\\.[0-9]+\\.[0-9]+" LIMIT_COUNT 1)
if(RPILEDCONFIG_README_LINE MATCHES "^\\*v([0-9]+\\.[0-9]+\\.[0-9]+)")
    rpiledconfig_expect("README.md" "${CMAKE_MATCH_1}")
else()
    rpiledconfig_expect("README.md" "(no *vX.Y.Z line)")
endif()

if(RPILEDCONFIG_VERSION_ERRORS)
    message(FATAL_ERROR "The version is ${RPILEDCONFIG_VERSION} (daemon/CMakeLists.txt), but:${RPILEDCONFIG_VERSION_ERRORS}")
endif()

message(STATUS "Version ${RPILEDCONFIG_VERSION}: daemon, app and README agree")
