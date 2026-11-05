//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "synth.h"

#include <algorithm>
#include <cmath>

namespace rpiledconfig::reader::test {

Id id_from(std::uint64_t value) {
  Id id{};
  for (int i = static_cast<int>(id_length) - 1; i >= 0; --i) {
    id[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value & 0xFF);
    value >>= 8;
  }
  return id;
}

std::vector<Sample> synthesize(const Id& id, const SynthOptions& o) {
  Lcg random(o.seed);
  const auto symbols = frame_symbols_for(id);
  const double period = static_cast<double>(symbols.size()) * o.symbol;

  const auto lit_at = [&](double at) {
    if (at < o.dark_ms) return false;
    for (const auto& cover : o.covers) {
      if (at >= cover.from_ms && at < cover.from_ms + cover.duration_ms) return false;
    }
    // The LED's own clock runs (1 + clock_error) times slower.
    const double led = (at - o.dark_ms + o.start_ms) / (1 + o.clock_error);
    double within = std::fmod(led, period);
    if (within < 0) within += period;
    const int h = std::min(static_cast<int>(symbols.size()) - 1, static_cast<int>(within / o.symbol));
    bool level = symbols[static_cast<std::size_t>(h)] != 0;
    if (led < period && std::find(o.corrupt_symbols.begin(), o.corrupt_symbols.end(), h) != o.corrupt_symbols.end()) level = !level;
    return level;
  };

  std::vector<Sample> out;
  const double interval = 1000.0 / o.fps;
  for (double nominal = 0; nominal < o.seconds * 1000; nominal += interval) {
    const double t = o.jitter_ms > 0 ? std::max(0.0, nominal + (random.next() - 0.5) * o.jitter_ms) : nominal;
    const double taken = o.content_jitter_ms > 0 ? t + (random.next() - 0.5) * o.content_jitter_ms : t;
    double lit;
    if (o.exposure_ms > 0) {
      constexpr int steps = 8;
      int count = 0;
      for (int i = 0; i < steps; ++i) {
        if (lit_at(taken - o.exposure_ms + (i + 0.5) * o.exposure_ms / steps)) ++count;
      }
      lit = std::min(1.0, static_cast<double>(count) / steps * o.saturation);
    } else {
      lit = lit_at(taken) ? 1 : 0;
    }
    double y = o.off + (o.on - o.off) * lit;
    if (o.noise > 0) y += (random.next() - 0.5) * o.noise;
    if (o.drift_amplitude > 0) y += o.drift_amplitude * std::sin(2 * 3.14159265358979 * t / (o.drift_period_s * 1000));
    out.push_back(Sample{t, std::clamp(y, 0.0, 255.0)});
  }
  return out;
}

}  // namespace rpiledconfig::reader::test
