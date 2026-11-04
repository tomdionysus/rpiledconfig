//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "tracker.h"

#include <gtest/gtest.h>

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
