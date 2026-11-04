//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "codec.h"
#include "protocol.h"

// What a camera would sample from a Pi's LED, made from the encoder: the same model as
// app/src/led/synth.ts. It is a kind one; recordings from real phones are the test that
// matters.
namespace rpiledconfig::reader::test {

struct Sample {
  double t;
  double y;
};

struct Window {
  double from_ms;
  double duration_ms;
};

struct SynthOptions {
  double fps = 30;
  double seconds = 20;
  double symbol = symbol_ms;
  // The LED's clock error: 0.05 runs it 5% slow.
  double clock_error = 0;
  // Peak-to-peak noise on each sample.
  double noise = 0;
  double off = 30;
  double on = 180;
  double drift_amplitude = 0;
  double drift_period_s = 9;
  // Where in the frame sampling begins, in ms of LED time.
  double start_ms = 0;
  double dark_ms = 0;
  // Peak-to-peak jitter on each timestamp, and on when the picture was really taken.
  double jitter_ms = 0;
  double content_jitter_ms = 0;
  // Windows of sampling time when a hand covers the LED.
  std::vector<Window> covers;
  // Each sample averages the LED over the exposure ending at its time; a saturating LED's lit
  // share is multiplied by `saturation` and clipped, the bloom a real camera shows.
  double exposure_ms = 0;
  double saturation = 1;
  // Symbols of frame 0 to invert.
  std::vector<int> corrupt_symbols;
  std::uint32_t seed = 12345;
};

class Lcg {
 public:
  explicit Lcg(std::uint32_t seed) : _state(seed) {}
  double next() {
    _state = _state * 1664525u + 1013904223u;
    return _state / 4294967296.0;
  }

 private:
  std::uint32_t _state;
};

std::vector<Sample> synthesize(const Id& id, const SynthOptions& options);

Id id_from(std::uint64_t value);

}  // namespace rpiledconfig::reader::test
