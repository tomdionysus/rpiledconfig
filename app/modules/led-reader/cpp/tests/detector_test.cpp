//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "detector.h"

#include <gtest/gtest.h>

#include <cmath>

#include "synth.h"

using namespace rpiledconfig::reader;

TEST(DetectorTest, FindsTheLedWithinThreeSecondsAndKeepsIt) {
  const auto samples = test::synthesize(test::id_from(0x0123456789abcdefULL), {.seconds = 20, .noise = 6});
  Detector detector;
  double found = -1;
  bool lost_after = false;
  for (const auto& s : samples) {
    const bool seen = detector.push(s.t, s.y);
    if (seen && found < 0) found = s.t;
    if (found >= 0 && !seen) lost_after = true;
  }
  EXPECT_GE(found, 0);
  EXPECT_LT(found, 3000);
  EXPECT_FALSE(lost_after);
}

TEST(DetectorTest, KeepsTheLedThroughABloomingPhoneCamera) {
  for (std::uint32_t seed = 1; seed <= 5; ++seed) {
    const auto samples = test::synthesize(test::id_from(0x64958d1c7ab3e205ULL), {.seconds = 30,
                                                                                    .noise = 12,
                                                                                    .off = 20,
                                                                                    .on = 69,
                                                                                    .jitter_ms = 3,
                                                                                    .content_jitter_ms = 8,
                                                                                    .exposure_ms = 12,
                                                                                    .saturation = 3,
                                                                                    .seed = seed});
    Detector detector;
    double found = -1;
    int lost = 0;
    for (const auto& s : samples) {
      const bool seen = detector.push(s.t, s.y);
      if (seen && found < 0) found = s.t;
      if (found >= 0 && !seen) ++lost;
    }
    EXPECT_GE(found, 0) << seed;
    EXPECT_LT(found, 3500) << seed;
    EXPECT_EQ(lost, 0) << seed;
  }
}

// A dark camera's noise and drift, a 1 Hz blink, a steady scene: never the LED.
TEST(DetectorTest, NoiseAndOtherLightsAreNotTheLed) {
  for (std::uint32_t seed = 1; seed <= 20; ++seed) {
    test::Lcg random(seed);
    Detector uniform;
    Detector gaussian;
    for (int i = 0; i < 30 * 30; ++i) {
      const double t = i * 1000.0 / 30;
      EXPECT_FALSE(uniform.push(t, 6 + 10 * std::sin(t / 3000) + (random.next() - 0.5) * 16)) << seed << " at " << t;
      double g = 0;
      for (int k = 0; k < 12; ++k) g += random.next();
      EXPECT_FALSE(gaussian.push(t, 50 + (g - 6) * 5)) << seed << " at " << t;
    }
  }
  Detector blink;
  for (int i = 0; i < 30 * 20; ++i) {
    const double t = i * 1000.0 / 30;
    EXPECT_FALSE(blink.push(t, static_cast<int>(t / 500) % 2 == 0 ? 200 : 20));
  }
}

TEST(DetectorTest, TheMeasuresOfTwoLevels) {
  EXPECT_DOUBLE_EQ(Detector::near_extremes({1, 1, 1, 1}), 1.0);
  EXPECT_GT(Detector::level_separation({0, 0, 1, 0, 100, 101, 100, 99, 0, 100}), 10);
  EXPECT_LT(Detector::level_separation({0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100}), 3);
}
