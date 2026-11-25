#
# RPiLEDConfig - app
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# The LED reader for Android: the C++ core in ../cpp and its JNI entry points.
# 3.22 rather than the 3.24 of the other trees: the Android Gradle plugin builds with the
# SDK's CMake, which is 3.22.1.
cmake_minimum_required(VERSION 3.22)
project(ledreader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../cpp)

add_library(ledreader SHARED
  src/main/cpp/jni.cpp
  ${CORE}/codec.cpp
  ${CORE}/detector.cpp
  ${CORE}/reader.cpp
  ${CORE}/tracker.cpp)
target_include_directories(ledreader PRIVATE ${CORE})
target_compile_options(ledreader PRIVATE -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Wthread-safety)
