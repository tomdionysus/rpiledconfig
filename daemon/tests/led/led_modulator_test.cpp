//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led/led_modulator.h"

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "led/led_fake.h"
#include "mocks/logger_mock.h"
#include "timers/timer_source.h"
#include "util.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

const auto vectors_id = machine_id::MachineId::from_value(0x0123456789abcdefULL);

// The frame of docs/protocol.md built the long way, from a string of half-bit levels:
// sync (1111 0000), start bit (10), then each bit of the codeword as 10 or 01, then idle
// (0000); runs are maximal stretches of one level. The codeword is the protocol's vector,
// computed outside this code base.
std::vector<led::Run> expected_runs() {
  const std::vector<std::uint8_t> bytes = *util::from_hex("52050123456789abcdef07a62dbecfdf83914ddbbd24721a");

  std::string levels =
      "11110000"
      "10";
  for (const auto byte : bytes) {
    for (int bit = 7; bit >= 0; --bit) levels += ((byte >> bit) & 1) ? "10" : "01";
  }
  levels += "0000";

  std::vector<led::Run> runs;
  for (const char level : levels) {
    const bool on = level == '1';
    if (!runs.empty() && runs.back().on == on) {
      runs.back().length++;
    } else {
      runs.push_back(led::Run{on, 1});
    }
  }
  return runs;
}

std::string describe(const std::vector<led::Run>& runs) {
  std::string out;
  for (const auto& run : runs) out += (run.on ? "on" : "off") + std::to_string(run.length) + " ";
  return out;
}

}  // namespace

TEST(LedModulatorTest, AFrameIs398HalfBitsOr19900Milliseconds) {
  EXPECT_EQ(led::frame_half_bits(), 398u);
  EXPECT_EQ(led::half_bit, 50ms);
  EXPECT_EQ(led::half_bit * led::frame_half_bits(), 19900ms);
}

TEST(LedModulatorTest, TheFrameRunsOfTheVectorsId) {
  const auto runs = led::frame_runs(vectors_id);
  const auto expected = expected_runs();

  EXPECT_EQ(runs, expected) << "got:      " << describe(runs) << "\nexpected: " << describe(expected);
  EXPECT_EQ(runs.size(), 294u);

  // docs/protocol.md: starting +4,-4,+1,-2,+2,-2,+2,-2 (the sync, the start bit whose off
  // half merges with 0x52's leading 0, then 0x52's alternating bits).
  const std::vector<led::Run> start{{true, 4}, {false, 4}, {true, 1}, {false, 2}, {true, 2}, {false, 2}, {true, 2}, {false, 2}};
  ASSERT_GE(runs.size(), start.size());
  EXPECT_EQ(std::vector<led::Run>(runs.begin(), runs.begin() + static_cast<std::ptrdiff_t>(start.size())), start);

  // ...and ending +2,-1,+1,-2,+2,-2,+1,-4: 0x1a's last bits, its final 0 ending on, then
  // the idle.
  const std::vector<led::Run> end{{true, 2}, {false, 1}, {true, 1}, {false, 2}, {true, 2}, {false, 2}, {true, 1}, {false, 4}};
  EXPECT_EQ(std::vector<led::Run>(runs.end() - static_cast<std::ptrdiff_t>(end.size()), runs.end()), end);

  int total = 0;
  for (const auto& run : runs) total += run.length;
  EXPECT_EQ(total, 398);
}

// Within data the LED is never in one state for more than 2T, so the sync's 4T runs can
// only be the sync.
TEST(LedModulatorTest, NoRunInsideTheDataIsLongerThanTwoHalfBits) {
  for (const auto id : {vectors_id, machine_id::MachineId::from_value(0), machine_id::MachineId::from_value(0xffffffffffffffffULL)}) {
    const auto runs = led::frame_runs(id);
    for (std::size_t i = 2; i + 1 < runs.size(); ++i) EXPECT_LE(runs[i].length, 2) << "run " << i << " of " << id.to_hex();
    EXPECT_EQ(runs[0].length, 4);
    EXPECT_EQ(runs[1].length, 4);
  }
}

TEST(LedModulatorTest, DrivesTheFakeThroughTheSameRunsOnAManualClock) {
  boost::asio::io_context io;
  auto logger = std::make_shared<MockLogger>();
  auto timers = std::make_shared<timers::ManualTimerSource>();
  auto led = std::make_shared<led::LedFake>([timers]() { return timers->now(); });
  auto modulator = std::make_shared<led::LedModulator>(logger, io.get_executor(), led, timers, vectors_id);

  ASSERT_TRUE(modulator->start());
  EXPECT_EQ(led->begins(), 1);
  EXPECT_TRUE(led->on()) << "the sync starts with the LED on";
  EXPECT_EQ(modulator->frames_started(), 1u);

  // One whole frame, half a bit at a time, as the kernel's timer would deliver it; the
  // next frame's first set() marks where this one ended.
  for (std::size_t i = 0; i < led::frame_half_bits(); ++i) timers->advance(led::half_bit);
  EXPECT_EQ(modulator->frames_started(), 2u);

  const auto& events = led->events();
  const auto expected = led::frame_runs(vectors_id);
  ASSERT_EQ(events.size(), expected.size() + 1);

  std::vector<led::Run> seen;
  for (std::size_t i = 0; i + 1 < events.size(); ++i) {
    const auto length = std::chrono::duration_cast<std::chrono::milliseconds>(events[i + 1].at - events[i].at) / led::half_bit;
    seen.push_back(led::Run{events[i].on, static_cast<int>(length)});
  }
  EXPECT_EQ(seen, expected) << "got:      " << describe(seen) << "\nexpected: " << describe(expected);
  EXPECT_TRUE(events.back().on) << "the next frame's sync";

  modulator->stop();
  EXPECT_FALSE(led->on());
  EXPECT_EQ(led->ends(), 1);
  EXPECT_FALSE(modulator->running());

  // Nothing more after stop, however long we wait.
  const auto count = led->events().size();
  timers->advance(10s);
  EXPECT_EQ(led->events().size(), count);
}

TEST(LedModulatorTest, FramesRepeatBackToBack) {
  boost::asio::io_context io;
  auto logger = std::make_shared<MockLogger>();
  auto timers = std::make_shared<timers::ManualTimerSource>();
  auto led = std::make_shared<led::LedFake>([timers]() { return timers->now(); });
  auto modulator = std::make_shared<led::LedModulator>(logger, io.get_executor(), led, timers, vectors_id);

  ASSERT_TRUE(modulator->start());
  const auto started = timers->now();

  timers->advance(3 * 19900ms);
  EXPECT_EQ(modulator->frames_started(), 4u);

  // The fourth frame's sync began exactly 3 frames after the first: no drift.
  const auto& events = led->events();
  const auto fourth = events[3 * led::frame_runs(vectors_id).size()];
  EXPECT_EQ(fourth.at - started, 3 * 19900ms);
  EXPECT_TRUE(fourth.on);

  modulator->stop();
}

TEST(LedModulatorTest, AnLedThatRefusesStopsTheModulatorBeforeItStarts) {
  boost::asio::io_context io;
  auto logger = std::make_shared<MockLogger>();
  auto timers = std::make_shared<timers::ManualTimerSource>();
  auto led = std::make_shared<led::LedFake>();
  led->fail_begin("no such node");
  auto modulator = std::make_shared<led::LedModulator>(logger, io.get_executor(), led, timers, vectors_id);

  const auto started = modulator->start();
  EXPECT_FALSE(started);
  EXPECT_EQ(started.error, "no such node");
  EXPECT_FALSE(modulator->running());
  EXPECT_EQ(timers->pending(), 0u);
  EXPECT_TRUE(led->events().empty());
}
