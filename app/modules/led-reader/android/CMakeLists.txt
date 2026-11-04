# The LED reader for Android: the C++ core in ../cpp and its JNI entry points.
cmake_minimum_required(VERSION 3.22)
project(ledreader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../cpp)

add_library(ledreader SHARED
  src/main/cpp/jni.cpp
  ${CORE}/codec.cpp
  ${CORE}/detector.cpp
  ${CORE}/reader.cpp
  ${CORE}/tracker.cpp)
target_include_directories(ledreader PRIVATE ${CORE})
target_compile_options(ledreader PRIVATE -O2 -Wall)
