# The LED reader's C++ core, built on the development machine with its GoogleTest suite.
# The phone builds the same sources through android/CMakeLists.txt (and the podspec on iOS).
#
#   cmake -S app/modules/led-reader/cpp -B app/modules/led-reader/cpp/build -G Ninja
#   cmake --build app/modules/led-reader/cpp/build && app/modules/led-reader/cpp/build/led_reader_tests
cmake_minimum_required(VERSION 3.20)
project(led_reader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_library(led_reader_core STATIC codec.cpp detector.cpp reader.cpp tracker.cpp)
target_include_directories(led_reader_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(led_reader_core PRIVATE -Wall -Wextra -Wpedantic)

find_package(GTest REQUIRED)
add_executable(led_reader_tests
  tests/codec_test.cpp
  tests/detector_test.cpp
  tests/reader_test.cpp
  tests/tracker_test.cpp
  tests/recordings_test.cpp
  tests/synth.cpp)
target_link_libraries(led_reader_tests PRIVATE led_reader_core GTest::gtest_main)
target_compile_options(led_reader_tests PRIVATE -Wall -Wextra)
target_compile_definitions(led_reader_tests PRIVATE LED_READER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../../src/led/fixtures")
