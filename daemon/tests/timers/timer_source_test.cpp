//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "timers/timer_source.h"

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <vector>

using namespace rpiledconfig::timers;
using namespace std::chrono_literals;

TEST(ManualTimerSourceTest, NothingFiresUntilTimeMoves) {
  auto timers = std::make_shared<ManualTimerSource>();
  bool fired = false;

  timers->schedule(500ms, [&fired]() { fired = true; });

  EXPECT_FALSE(fired);
  EXPECT_EQ(timers->pending(), 1u);

  timers->advance(499ms);
  EXPECT_FALSE(fired);

  timers->advance(1ms);
  EXPECT_TRUE(fired);
  EXPECT_EQ(timers->pending(), 0u);
}

TEST(ManualTimerSourceTest, FiresInDueOrder) {
  auto timers = std::make_shared<ManualTimerSource>();
  std::vector<int> order;

  timers->schedule(300ms, [&order]() { order.push_back(3); });
  timers->schedule(100ms, [&order]() { order.push_back(1); });
  timers->schedule(200ms, [&order]() { order.push_back(2); });

  timers->advance(1s);

  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);
  EXPECT_EQ(order[2], 3);
}

TEST(ManualTimerSourceTest, CancelStopsATimerOnce) {
  auto timers = std::make_shared<ManualTimerSource>();
  bool fired = false;

  auto timer = timers->schedule(100ms, [&fired]() { fired = true; });
  EXPECT_TRUE(timer->cancel());
  EXPECT_FALSE(timer->cancel());

  timers->advance(1s);
  EXPECT_FALSE(fired);
}

// A callback that re-arms, as the modulator does for every run, sees the time its own timer
// fired and the next one falls due in the same advance.
TEST(ManualTimerSourceTest, CallbacksMaySeeTheirOwnTimeAndReArm) {
  auto timers = std::make_shared<ManualTimerSource>();
  const auto started = timers->now();
  std::vector<std::chrono::milliseconds> seen;

  std::function<void()> rearm = [&]() {
    seen.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(timers->now() - started));
    if (seen.size() < 4) timers->schedule(50ms, rearm);
  };

  timers->schedule(50ms, rearm);
  timers->advance(1s);

  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0], 50ms);
  EXPECT_EQ(seen[3], 200ms);
  EXPECT_EQ(timers->now() - started, 1s);
}

TEST(AsioTimerSourceTest, FiresOnItsExecutorAndCancels) {
  boost::asio::io_context io;
  auto timers = std::make_shared<AsioTimerSource>(io.get_executor());
  int fired = 0;

  timers->schedule(1ms, [&fired]() { ++fired; });
  auto cancelled = timers->schedule(1ms, [&fired]() { fired += 100; });
  EXPECT_TRUE(cancelled->cancel());

  io.run();

  EXPECT_EQ(fired, 1);
  EXPECT_FALSE(cancelled->cancel());
}
