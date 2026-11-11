//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "tracker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "synth.h"

using namespace rpiledconfig::reader;

namespace {

const Id vectors_id = test::id_from(0x0123456789abcdefULL);

struct Scene {
  int width = 320;
  int height = 240;
  // The LED: a blob of this radius at (x, y), its brightness over the background.
  int led_x = 250;
  int led_y = 60;
  int led_radius = 3;
  double led_gain = 120;
  double background = 90;
  double noise = 10;
};

// Frames of a scene with the Pi's LED off centre, a lamp blinking at 1 Hz elsewhere, a
// brightness gradient, and noise; the LED's level per frame comes from the camera model.
std::vector<std::uint8_t> render(const Scene& scene, double lit, double t, test::Lcg& random) {
  std::vector<std::uint8_t> plane(static_cast<std::size_t>(scene.width * scene.height));
  const bool lamp = static_cast<int>(t / 500) % 2 == 0;
  for (int y = 0; y < scene.height; ++y) {
    for (int x = 0; x < scene.width; ++x) {
      double v = scene.background + 40.0 * x / scene.width + (random.next() - 0.5) * scene.noise;
      const double dl = std::hypot(x - scene.led_x, y - scene.led_y);
      if (dl <= scene.led_radius) v += scene.led_gain * lit;
      if (lamp && std::hypot(x - 60, y - 180) <= 8) v += 100;
      plane[static_cast<std::size_t>(y * scene.width + x)] = static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
    }
  }
  return plane;
}

}  // namespace

TEST(TrackerTest, FindsTheLedOffCentreAndReadsItInUnderTenSeconds) {
  const Scene scene;
  // The camera model gives the LED's lit share per frame, from 0 to 1.
  const auto levels = test::synthesize(vectors_id, {.seconds = 14, .off = 0, .on = 1, .start_ms = 3100, .jitter_ms = 3, .exposure_ms = 20, .saturation = 2});
  Tracker tracker;
  test::Lcg random(5);
  std::optional<double> read_at;
  std::optional<TileRect> tile;
  for (const auto& s : levels) {
    const auto plane = render(scene, s.y, s.t, random);
    const auto& state = tracker.push(s.t, Plane{plane.data(), scene.width, scene.height, scene.width, 1});
    if (state.reader.result && !read_at) {
      read_at = s.t;
      EXPECT_EQ(state.reader.result->id, vectors_id);
      tile = state.tile;
    }
  }
  ASSERT_TRUE(read_at);
  EXPECT_LT(*read_at, 10000);
  ASSERT_TRUE(tile);
  EXPECT_LE(tile->x, scene.led_x);
  EXPECT_GT(tile->x + tile->width, scene.led_x - scene.led_radius);
  EXPECT_LE(tile->y, scene.led_y + scene.led_radius);
  EXPECT_GT(tile->y + tile->height, scene.led_y - scene.led_radius);
}

TEST(TrackerTest, ALampBlinkingAtOneHertzAndNoiseAreNotTheLed) {
  Scene scene;
  scene.led_gain = 0;
  Tracker tracker;
  test::Lcg random(9);
  for (double t = 0; t < 8000; t += 1000.0 / 30) {
    const auto plane = render(scene, 0, t, random);
    const auto& state = tracker.push(t, Plane{plane.data(), scene.width, scene.height, scene.width, 1});
    EXPECT_FALSE(state.tile) << t;
    EXPECT_FALSE(state.reader.led_seen) << t;
  }
}

// The box on screen marks the LED itself, not the tile it is in, even with a steady light brighter
// than it just beside it, or a reflection blinking with it.
TEST(TrackerTest, TheSpotIsTheLedAndNotTheTileOrALampBesideIt) {
  constexpr int width = 320;
  constexpr int height = 240;
  // Well off the middle of its tile (40 to 80, 80 to 120); the lamp is in the next tile.
  constexpr int led_x = 73;
  constexpr int led_y = 87;
  const auto levels = test::synthesize(vectors_id, {.seconds = 14, .off = 0, .on = 1, .start_ms = 700, .jitter_ms = 3});

  std::vector<std::uint8_t> picture(static_cast<std::size_t>(width * height));
  Tracker tracker;
  for (const auto& s : levels) {
    std::fill(picture.begin(), picture.end(), std::uint8_t{30});
    const auto paint = [&](int cx, int cy, int radius, std::uint8_t value) {
      for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) picture[static_cast<std::size_t>(y * width + x)] = value;
      }
    };
    paint(88, 100, 4, 255);
    // The LED, and a reflection of it that blinks with it, dimmer, up and to one side.
    if (s.y > 0.5) {
      paint(led_x + 16, led_y - 18, 5, 150);
      paint(led_x, led_y, 3, 200);
    }
    tracker.push(s.t, Plane{picture.data(), width, height, width, 1});
  }

  const auto& state = tracker.state();
  ASSERT_TRUE(state.tile);
  EXPECT_EQ(state.tile->x, 40);
  EXPECT_EQ(state.tile->y, 80);
  ASSERT_TRUE(state.spot);
  EXPECT_NEAR(state.spot->x, led_x, 2);
  EXPECT_NEAR(state.spot->y, led_y, 2);
}

// A longer T needs a longer history, and everything else in the tracker scales with it.
TEST(TrackerTest, ReadsAtEveryRate) {
  for (const double symbol : {33.0, 51.0, 77.0}) {
    test::SynthOptions options;
    options.symbol = symbol;
    options.seconds = symbol / 1000 * 170 * 2.2;
    options.start_ms = 1100;
    options.jitter_ms = 3;
    const auto levels = test::synthesize(vectors_id, options);
    Tracker tracker(TrackerConfig{.symbol = symbol});
    bool read = false;
    for (const auto& s : levels) read = tracker.push_values(s.t, 1, 1, {s.y}).reader.result.has_value() || read;
    EXPECT_TRUE(read) << "T = " << symbol;
  }
}

// The phone moved: the LED has left the tile being read and is in another. After a second
// gone, the other tile is read instead.
TEST(TrackerTest, FollowsTheLedToAnotherTileOnceItHasBeenGoneASecond) {
  // Two tiles side by side; the LED a small square in the left one, then in the right.
  constexpr int width = 80;
  constexpr int height = 40;
  const auto levels = test::synthesize(vectors_id, {.seconds = 24, .off = 0, .on = 1, .start_ms = 1100, .jitter_ms = 3});
  const double moved_at = 4000;
  std::vector<std::uint8_t> picture(static_cast<std::size_t>(width * height));
  Tracker tracker;
  std::optional<int> before;
  std::optional<int> after;
  bool read = false;
  for (const auto& s : levels) {
    const bool moved = s.t >= moved_at;
    std::fill(picture.begin(), picture.end(), std::uint8_t{30});
    if (s.y > 0.5) {
      const int cx = moved ? 60 : 20;
      for (int y = 17; y <= 23; ++y) {
        for (int x = cx - 3; x <= cx + 3; ++x) picture[static_cast<std::size_t>(y * width + x)] = 200;
      }
    }
    const auto& state = tracker.push(s.t, Plane{picture.data(), width, height, width, 1});
    if (!state.tile) continue;
    if (!moved) before = state.tile->x;
    if (s.t >= moved_at + 2000) after = state.tile->x;
    read = (moved && state.reader.result.has_value()) || read;
  }
  ASSERT_TRUE(before && after);
  EXPECT_NE(*before, *after);
  EXPECT_TRUE(read);
}
