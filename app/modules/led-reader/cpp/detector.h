//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "protocol.h"

// Whether one stream of brightnesses (a tile of the camera's frames) holds this protocol's
// LED, judged over the last second from the samples alone (docs/protocol.md, "Decoding").
//
// Two measures of two levels: the separation of the halves' medians in their median
// absolute deviations, and the share of samples near the 5th or 95th percentile. Measured
// over 1 s windows: a Blackview A85 on a Pi 5's LED, separation median 14, lowest 2.05,
// near the extremes median 0.65 to 0.74, lowest 0.45 (a third of its frames straddle an
// edge, at 30 fps and T = 45 ms); uniform and Gaussian noise, separation median 2, up to
// 5.2, near the extremes up to 0.58. Both to find, either to keep, and noise never passed
// the bar to find for longer than 533 ms in 40 minutes of it, hence the 800 ms hold.
namespace rpiledconfig::reader {

struct DetectorConfig {
  double symbol = symbol_ms;
};

class Detector {
 public:
  explicit Detector(DetectorConfig config = {});

  // Feeds one sample; returns whether the LED is seen after it.
  bool push(double t, double y);

  bool seen() const { return _seen; }
  // The adaptive threshold's state: the range seen, the level of the last sample with
  // hysteresis, and the middle the deviations are taken from.
  double contrast() const { return _hi - _lo; }
  double middle() const { return (_lo + _hi) / 2; }
  bool level() const { return _level; }
  // Whether the stream fell dark (contrast below the minimum) on the last push: what was
  // gathered from it should be forgotten.
  bool went_dark() const { return _went_dark; }
  // When the stream began looking like the LED without a break, once seen; the samples since
  // a window before then are code seen.
  std::optional<double> seen_since() const { return _seen_since; }

  static double level_separation(std::vector<double> values);
  static double near_extremes(std::vector<double> values);

 private:
  void _reset();

  DetectorConfig _config;
  bool _have_range = false;
  double _lo = 0;
  double _hi = 0;
  double _last_t = 0;
  bool _level = false;
  bool _active = false;
  double _first_t = 0;
  double _run_start = 0;
  struct Run {
    double end;
    double length;
  };
  std::deque<Run> _runs;
  struct Sample {
    double t;
    double y;
  };
  std::deque<Sample> _recent;
  std::optional<double> _finding_since;
  std::optional<double> _seen_since;
  bool _seen = false;
  bool _went_dark = false;
};

}  // namespace rpiledconfig::reader
