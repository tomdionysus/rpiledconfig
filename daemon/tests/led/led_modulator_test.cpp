//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
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

const auto vectors_id = pairing_key::PairingKey::from_value(0x0123456789abcdefULL);

// The frame of docs/protocol.md, frame version 7, as its 170 symbols, computed outside this
// code base; runs are maximal stretches of one level.
const std::string vector_symbols =
    "11111000000010000101001100100101010010110110001101100101001110100101011011011001110101101100100001001001010110101100101000110001001010110101001001101100101010111011010011";

std::vector<led::Run> expected_runs() {
  std::vector<led::Run> runs;
  for (const char level : vector_symbols) {
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

// docs/protocol.md, Symbols.
TEST(LedModulatorTest, TheRatesAreTheProtocols) {
  EXPECT_EQ(led::blink_rate(0x01), 51ms);
  EXPECT_EQ(led::blink_rate(0x02), 33ms);
  EXPECT_EQ(led::blink_rate(0x03), 77ms);
  EXPECT_EQ(led::blink_rate(0x00), std::nullopt);
  EXPECT_EQ(led::blink_rate(0x04), std::nullopt);
}

TEST(LedModulatorTest, AFrameIs170SymbolsOr8670MillisecondsAtTheNormalRate) {
  EXPECT_EQ(led::frame_symbols(), 170u);
  EXPECT_EQ(led::symbol_time, 51ms);
  EXPECT_EQ(led::symbol_time * led::frame_symbols(), 8670ms);
}

TEST(LedModulatorTest, TheFrameRunsOfTheVectorsId) {
  const auto runs = led::frame_runs(vectors_id);
  const auto expected = expected_runs();

  EXPECT_EQ(runs, expected) << "got:      " << describe(runs) << "\nexpected: " << describe(expected);
  EXPECT_EQ(runs.size(), 107u);

  // docs/protocol.md: starting +5,-7,+1,-4,+1,-1,+1,-2 (the sync, its off joined by the
  // first word's leading 0s) and ending -1,+3,-1,+2,-1,+1,-2,+2.
  const std::vector<led::Run> start{{true, 5}, {false, 7}, {true, 1}, {false, 4}, {true, 1}, {false, 1}, {true, 1}, {false, 2}};
  ASSERT_GE(runs.size(), start.size());
  EXPECT_EQ(std::vector<led::Run>(runs.begin(), runs.begin() + static_cast<std::ptrdiff_t>(start.size())), start);
  const std::vector<led::Run> end{{false, 1}, {true, 3}, {false, 1}, {true, 2}, {false, 1}, {true, 1}, {false, 2}, {true, 2}};
  EXPECT_EQ(std::vector<led::Run>(runs.end() - static_cast<std::ptrdiff_t>(end.size()), runs.end()), end);

  int total = 0;
  for (const auto& run : runs) total += run.length;
  EXPECT_EQ(total, 170);
}

// Words begin and end with runs of at most two, so within data the LED holds one state for
// at most 4T, whatever the id, and only the sync has runs of 5T or more.
TEST(LedModulatorTest, NoRunInsideTheDataIsLongerThanFourSymbols) {
  for (const auto id : {vectors_id, pairing_key::PairingKey::from_value(0), pairing_key::PairingKey::from_value(0xffffffffffffffffULL),
                        pairing_key::PairingKey::from_value(0x5555555555555555ULL)}) {
    const auto runs = led::frame_runs(id);
    EXPECT_EQ(runs[0].on, true);
    EXPECT_GE(runs[0].length, 5);
    EXPECT_GE(runs[1].length, 5);
    for (std::size_t i = 2; i < runs.size(); ++i) EXPECT_LE(runs[i].length, 4) << "run " << i << " of " << id.to_hex();
  }
}

TEST(LedModulatorTest, DrivesTheFakeThroughTheSameRunsOnAManualClock) {
  boost::asio::io_context io;
  auto logger = std::make_shared<MockLogger>();
  auto timers = std::make_shared<timers::ManualTimerSource>();
  auto led = std::make_shared<led::LedFake>([timers]() { return timers->now(); });
  auto modulator = std::make_shared<led::LedModulator>(logger, io.get_executor(), led, timers);

  ASSERT_TRUE(modulator->start(vectors_id));
  EXPECT_EQ(led->begins(), 1);
  EXPECT_TRUE(led->on()) << "the sync starts with the LED on";
  EXPECT_EQ(modulator->frames_started(), 1u);

  // One whole frame, a symbol at a time, as the kernel's timer would deliver it; the
  // next frame's first set() marks where this one ended.
  for (std::size_t i = 0; i < led::frame_symbols(); ++i) timers->advance(led::symbol_time);
  EXPECT_EQ(modulator->frames_started(), 2u);

  const auto& events = led->events();
  const auto expected = led::frame_runs(vectors_id);
  ASSERT_EQ(events.size(), expected.size() + 1);

  std::vector<led::Run> seen;
  for (std::size_t i = 0; i + 1 < events.size(); ++i) {
    const auto length = std::chrono::duration_cast<std::chrono::milliseconds>(events[i + 1].at - events[i].at) / led::symbol_time;
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
  auto modulator = std::make_shared<led::LedModulator>(logger, io.get_executor(), led, timers);

  ASSERT_TRUE(modulator->start(vectors_id));
  const auto started = timers->now();

  timers->advance(3 * 8670ms);
  EXPECT_EQ(modulator->frames_started(), 4u);

  // The fourth frame's sync began exactly 3 frames after the first: no drift.
  const auto& events = led->events();
  const auto fourth = events[3 * led::frame_runs(vectors_id).size()];
  EXPECT_EQ(fourth.at - started, 3 * 8670ms);
  EXPECT_TRUE(fourth.on);

  modulator->stop();
}

TEST(LedModulatorTest, AnLedThatRefusesStopsTheModulatorBeforeItStarts) {
  boost::asio::io_context io;
  auto logger = std::make_shared<MockLogger>();
  auto timers = std::make_shared<timers::ManualTimerSource>();
  auto led = std::make_shared<led::LedFake>();
  led->fail_begin("no such node");
  auto modulator = std::make_shared<led::LedModulator>(logger, io.get_executor(), led, timers);

  const auto started = modulator->start(vectors_id);
  EXPECT_FALSE(started);
  EXPECT_EQ(started.error, "no such node");
  EXPECT_FALSE(modulator->running());
  EXPECT_EQ(timers->pending(), 0u);
  EXPECT_TRUE(led->events().empty());
}
