#
# RPiLEDConfig - app
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# The LED reader's C++ core, built on the development machine with its GoogleTest suite.
# The phone builds the same sources through android/CMakeLists.txt (and the podspec on iOS).
#
#   cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja
#   cmake --build app/modules/led-reader/cpp/build && app/modules/led-reader/cpp/build/led_reader_tests
cmake_minimum_required(VERSION 3.24...4.4)

# The same number as daemon/CMakeLists.txt, set by hand at a release: nothing checks this one.
project(led_reader VERSION 0.5.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules: without this, CMake 3.28 and later with Ninja need clang-scan-deps to
# build with clang (daemon/CMakeLists.txt says more).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(RPILEDCONFIG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(led_reader_warnings INTERFACE)
target_compile_options(led_reader_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(led_reader_warnings INTERFACE -Wthread-safety)
endif()
if(RPILEDCONFIG_WARNINGS_AS_ERRORS)
  target_compile_options(led_reader_warnings INTERFACE -Werror)
endif()

add_library(led_reader_core STATIC codec.cpp detector.cpp reader.cpp tracker.cpp)
target_include_directories(led_reader_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(led_reader_core PRIVATE led_reader_warnings)

find_package(GTest REQUIRED)
set(LED_READER_TEST_SOURCES
  tests/codec_test.cpp
  tests/detector_test.cpp
  tests/reader_test.cpp
  tests/tracker_test.cpp
  tests/recordings_test.cpp
  tests/synth.cpp)
add_executable(led_reader_tests ${LED_READER_TEST_SOURCES})
target_link_libraries(led_reader_tests PRIVATE led_reader_core led_reader_warnings GTest::gtest_main)
# The tests set what they need of SynthOptions by designated initializers and leave the rest
# at their defaults, which -Wextra reports field by field. On the sources rather than the
# target, as a source's options come after the -Wextra the warnings target adds.
set_source_files_properties(${LED_READER_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
target_compile_definitions(led_reader_tests PRIVATE LED_READER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../../src/led/fixtures")
