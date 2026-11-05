//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "reader.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>

#include "synth.h"

using namespace rpiledconfig::reader;

namespace {

const Id vectors_id = test::id_from(0x0123456789abcdefULL);

struct Outcome {
  std::optional<double> read_at;
  std::set<Id> ids;
  ReaderState final;
};

Outcome run(const std::vector<test::Sample>& samples, ReaderConfig config = {}) {
  Reader reader(config);
  Outcome out;
  for (const auto& s : samples) {
    const auto& state = reader.push(s.t, s.y);
    if (state.result) {
      out.ids.insert(state.result->id);
      if (!out.read_at) out.read_at = s.t;
    }
    out.final = state;
  }
  return out;
}

// The goal: a capture in under ten seconds from first sight.
constexpr double goal_ms = 10000;

}  // namespace

TEST(ReaderTest, ReadsACleanStreamInUnderAFrame) {
  const auto out = run(test::synthesize(vectors_id, {.seconds = 12}));
  ASSERT_EQ(out.ids, std::set<Id>{vectors_id});
  EXPECT_LT(*out.read_at, frame_symbols * symbol_ms + 500);
}

// From wherever the camera comes in: the tail before the first sync counts.
TEST(ReaderTest, ReadsInUnderTenSecondsFromAnyStart) {
  for (double start = 0; start < frame_symbols * symbol_ms; start += 700) {
    const auto out = run(test::synthesize(vectors_id, {.seconds = 14, .noise = 6, .start_ms = start, .jitter_ms = 3, .seed = static_cast<std::uint32_t>(start)}));
    ASSERT_EQ(out.ids, std::set<Id>{vectors_id}) << "start " << start;
    EXPECT_LT(*out.read_at, goal_ms) << "start " << start;
  }
}

TEST(ReaderTest, ReadsAt24FpsWithTheClockFivePercentOffEitherWayAndAt60Fps) {
  for (const double error : {0.05, -0.05}) {
    const auto out = run(test::synthesize(vectors_id, {.fps = 24, .seconds = 14, .clock_error = error, .noise = 8, .start_ms = 3000, .jitter_ms = 4}));
    ASSERT_EQ(out.ids, std::set<Id>{vectors_id}) << error;
    EXPECT_LT(*out.read_at, goal_ms * 1.05) << error;
  }
  const auto fast = run(test::synthesize(vectors_id, {.fps = 60, .seconds = 14, .noise = 20, .jitter_ms = 2}));
  ASSERT_EQ(fast.ids, std::set<Id>{vectors_id});
  EXPECT_LT(*fast.read_at, goal_ms);
}

// Calibrated to a Blackview A85 on a Pi 5's LED at 30 fps: a saturated LED blooming over a
// 33 ms exposure, plus a lit and noisy room, the LED only partly inside the region, and some
// timing error on the pictures.
// The phone-camera model's exposure: a bright LED in a dim room has the camera's
// auto-exposure well under a frame. 33 ms (a whole frame) predicted that nothing under 50 ms
// would read at 30 fps; the A85 then read 45 and 33 ms symbols, so the model is held to 12 ms, which reads what the phone read.
TEST(ReaderTest, ReadsWhatARealPhoneCameraSeesInUnderTenSeconds) {
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    const auto out = run(test::synthesize(test::id_from(0x64958d1c7ab3e205ULL), {.seconds = 20,
                                                                                    .noise = 12,
                                                                                    .off = 20,
                                                                                    .on = 69,
                                                                                    .start_ms = 900.0 * seed,
                                                                                    .jitter_ms = 3,
                                                                                    .content_jitter_ms = 8,
                                                                                    .exposure_ms = 12,
                                                                                    .saturation = 3,
                                                                                    .seed = seed}));
    ASSERT_EQ(out.ids, std::set<Id>{test::id_from(0x64958d1c7ab3e205ULL)}) << seed;
    EXPECT_LT(*out.read_at, goal_ms) << seed;
  }
}

// A bright room: the LED a small step over a bright background, with drift.
TEST(ReaderTest, ReadsALowContrastLedInABrightDriftingRoom) {
  for (std::uint32_t seed = 1; seed <= 5; ++seed) {
    const auto out = run(test::synthesize(vectors_id, {.seconds = 20,
                                                       .noise = 8,
                                                       .off = 170,
                                                       .on = 215,
                                                       .drift_amplitude = 15,
                                                       .start_ms = 1300.0 * seed,
                                                       .jitter_ms = 3,
                                                       .exposure_ms = 10,
                                                       .seed = seed}));
    ASSERT_EQ(out.ids, std::set<Id>{vectors_id}) << seed;
    EXPECT_LT(*out.read_at, goal_ms) << seed;
  }
}

TEST(ReaderTest, ReadsThroughAHandOverTheLed) {
  const auto out = run(test::synthesize(vectors_id, {.seconds = 20, .noise = 6, .start_ms = 2000, .covers = {{3000, 1000}}}));
  ASSERT_EQ(out.ids, std::set<Id>{vectors_id});
  EXPECT_LT(*out.read_at, goal_ms + 1000);
}

TEST(ReaderTest, RepairsWrongSymbols) {
  // A wrong symbol in each of three words, confidently wrong.
  const auto out = run(test::synthesize(vectors_id, {.seconds = 12, .corrupt_symbols = {12, 61, 133}}));
  ASSERT_EQ(out.ids, std::set<Id>{vectors_id});
}

TEST(ReaderTest, NeverReadsAWrongIdFromAnotherLight) {
  test::Lcg random(77);
  std::vector<bool> levels;
  for (int i = 0; i < 4000; ++i) levels.push_back(random.next() < 0.5);
  std::vector<test::Sample> samples;
  for (double t = 0; t < 120000; t += 1000.0 / 30) samples.push_back({t, levels[static_cast<std::size_t>(t / symbol_ms) % levels.size()] ? 180.0 : 30.0});
  EXPECT_TRUE(run(samples).ids.empty());

  std::vector<test::Sample> blink;
  for (double t = 0; t < 60000; t += 1000.0 / 30) blink.push_back({t, static_cast<int>(t / 500) % 2 == 0 ? 180.0 : 30.0});
  EXPECT_TRUE(run(blink).ids.empty());
}

TEST(ReaderTest, ProgressIsZeroUntilTheLedAndFullWithinAFrameOfIt) {
  const double dark = 3000;
  const auto samples = test::synthesize(vectors_id, {.seconds = 15, .noise = 6, .start_ms = 4000, .dark_ms = dark});
  Reader reader;
  double previous = 0;
  std::optional<double> full;
  for (const auto& s : samples) {
    const auto& state = reader.push(s.t, s.y);
    if (s.t < dark) EXPECT_EQ(state.coverage, 0) << s.t;
    if (!state.result) EXPECT_GE(state.coverage, previous) << s.t;
    previous = state.coverage;
    if (!full && state.coverage >= 1) full = s.t;
    if (state.result) break;
  }
}

// Symbol times on the model: what each T
// reads at 30 and 60 fps. Printed, not asserted: the live phone decided (45 ms).
TEST(ReaderTest, SymbolTimeTrials) {
  for (const double symbol : {77.0, 51.0, 40.0, 33.0, 25.0, 20.0}) {
    for (const double fps : {30.0, 60.0}) {
      int read = 0;
      double slowest = 0;
      for (std::uint32_t seed = 1; seed <= 8; ++seed) {
        const auto out = run(test::synthesize(test::id_from(0x64958d1c7ab3e205ULL), {.fps = fps,
                                                                                        .seconds = 20,
                                                                                        .symbol = symbol,
                                                                                        .noise = 12,
                                                                                        .off = 20,
                                                                                        .on = 69,
                                                                                        .start_ms = 700.0 * seed,
                                                                                        .jitter_ms = 3,
                                                                                        .content_jitter_ms = fps > 30 ? 4.0 : 8.0,
                                                                                        .exposure_ms = fps > 30 ? 8.0 : 12.0,
                                                                                        .saturation = 3,
                                                                                        .seed = seed}),
                             ReaderConfig{symbol});
        if (out.ids == std::set<Id>{test::id_from(0x64958d1c7ab3e205ULL)}) {
          ++read;
          slowest = std::max(slowest, *out.read_at);
        }
        EXPECT_LE(out.ids.size(), 1u) << "a wrong ID at T " << symbol << " fps " << fps << " seed " << seed;
      }
      std::printf("[ trial    ] T = %.0f ms at %.0f fps: %d of 8 read, slowest at %.0f ms\n", symbol, fps, read, slowest);
    }
  }
}

// A symbol time whose frame is a whole number of camera frames is sampled at the same points on
// every repeat, so repeats add nothing if those points are bad ones. Every phase of the camera
// against the LED, on the phone-camera model: how many read, and how long the slowest took.
TEST(ReaderTest, RatePhaseTrials) {
  for (const double symbol : {33.0, 50.0, 51.0, 77.0}) {
    int read = 0;
    int phases = 0;
    double slowest = 0;
    double total = 0;
    for (int phase = 0; phase < 34; ++phase) {
      for (const double exposure : {0.0, 12.0}) {
        for (std::uint32_t seed = 1; seed <= 3; ++seed) {
          ++phases;
          const auto out = run(test::synthesize(vectors_id, {.seconds = 4 * 170 * symbol / 1000,
                                                             .symbol = symbol,
                                                             .noise = 6,
                                                             .start_ms = static_cast<double>(phase),
                                                             .jitter_ms = 3,
                                                             .exposure_ms = exposure,
                                                             .saturation = exposure > 0 ? 3.0 : 1.0,
                                                             .seed = seed * 7 + static_cast<std::uint32_t>(phase)}),
                               ReaderConfig{symbol});
          if (out.read_at) {
            ++read;
            slowest = std::max(slowest, *out.read_at);
            total += *out.read_at;
          }
        }
      }
    }
    // 50 ms is here as the warning: its frame is 255 camera frames at 30 fps. The rest are the rates.
    if (symbol != 50.0) EXPECT_EQ(read, phases) << "T = " << symbol;
    std::printf("[ phases   ] T = %2.0f ms (frame %5.2f s, %6.1f camera frames): %3d of %d read, mean %5.0f ms, slowest %6.0f ms\n", symbol, 170 * symbol / 1000, 170 * symbol * 30 / 1000, read, phases, read ? total / read : 0, slowest);
  }
}
