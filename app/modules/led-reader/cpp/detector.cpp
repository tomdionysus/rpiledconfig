//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "detector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rpiledconfig::reader {

namespace {

// Time constant of the running minimum and maximum: well over the longest steady stretch
// (the sync's 7T), so neither extreme decays into the threshold mid-frame.
constexpr double range_tau_ms = 2000;
// Below this swing the region is not looking at a blinking LED.
constexpr double min_contrast = 10;
// Hysteresis of the level, as a fraction of the range.
constexpr double hysteresis = 0.15;
constexpr double window_ms = 1000;
constexpr double shortest_run_ms = 30;
constexpr int min_runs = 4;
constexpr double find_separation = 4;
constexpr double find_near_extremes = 0.5;
constexpr double keep_separation = 2.5;
constexpr double keep_near_extremes = 0.45;
constexpr double find_hold_ms = 800;
constexpr double near_band = 0.12;

double median_of(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  const std::size_t mid = v.size() / 2;
  return v.size() % 2 == 1 ? v[mid] : (v[mid - 1] + v[mid]) / 2;
}

}  // namespace

Detector::Detector(DetectorConfig config) : _config(config) {}

double Detector::level_separation(std::vector<double> values) {
  if (values.empty()) return 0;
  const auto [lo_it, hi_it] = std::minmax_element(values.begin(), values.end());
  const double middle = (*lo_it + *hi_it) / 2;
  std::vector<double> low;
  std::vector<double> high;
  for (const double v : values) (v < middle ? low : high).push_back(v);
  if (low.size() < 3 || high.size() < 3) return 0;
  const double low_median = median_of(low);
  const double high_median = median_of(high);
  for (double& v : low) v = std::abs(v - low_median);
  for (double& v : high) v = std::abs(v - high_median);
  const double spread = median_of(low) + median_of(high);
  return spread > 0 ? (high_median - low_median) / spread : std::numeric_limits<double>::infinity();
}

double Detector::near_extremes(std::vector<double> values) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const std::size_t last = values.size() - 1;
  const double lo = values[static_cast<std::size_t>(std::floor(0.05 * static_cast<double>(last)))];
  const double hi = values[static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(last)))];
  if (hi <= lo) return 1;
  const double band = near_band * (hi - lo);
  std::size_t near = 0;
  for (const double v : values) {
    if (std::abs(v - lo) <= band || std::abs(v - hi) <= band) ++near;
  }
  return static_cast<double>(near) / static_cast<double>(values.size());
}

void Detector::_reset() {
  _active = false;
  _runs.clear();
  _recent.clear();
  _finding_since.reset();
  _seen_since.reset();
  _seen = false;
}

bool Detector::push(double t, double y) {
  _went_dark = false;
  if (!_have_range) {
    _lo = _hi = y;
    _have_range = true;
    _last_t = t;
    return false;
  }
  const double dt = std::max(0.0, t - _last_t);
  _last_t = t;
  const double decay = 1 - std::exp(-dt / range_tau_ms);
  _lo = std::min(y, _lo + (y - _lo) * decay);
  _hi = std::max(y, _hi - (_hi - y) * decay);
  const double range = _hi - _lo;

  if (range < min_contrast) {
    if (_active) {
      _reset();
      _went_dark = true;
    }
    return false;
  }

  const double mid = (_lo + _hi) / 2;
  const double band = range * hysteresis;
  if (!_active) {
    _active = true;
    _first_t = t;
    _run_start = t;
  }
  const bool was_on = _level;
  if (!_level && y > mid + band) _level = true;
  else if (_level && y < mid - band) _level = false;
  if (_level != was_on) {
    _runs.push_back(Run{t, t - _run_start});
    _run_start = t;
  }
  while (!_runs.empty() && _runs.front().end < t - window_ms) _runs.pop_front();
  _recent.push_back(Sample{t, y});
  while (_recent.front().t < t - window_ms) _recent.pop_front();

  // Runs of 1T to 7T (the sync's longest, a word's bits joined to it) with a camera frame's
  // slack, at least four, and none and no current run longer.
  const double longest = 7 * _config.symbol + 40;
  bool blinking = t - _first_t >= window_ms && t - _run_start <= longest;
  if (blinking) {
    int plausible = 0;
    for (const auto& run : _runs) {
      if (run.length > longest) blinking = false;
      if (run.length >= shortest_run_ms) ++plausible;
    }
    blinking = blinking && plausible >= min_runs;
  }
  if (blinking) {
    std::vector<double> values;
    values.reserve(_recent.size());
    for (const auto& s : _recent) values.push_back(s.y);
    const double separation = level_separation(values);
    const double near = near_extremes(values);
    blinking = _seen ? (separation >= keep_separation || near >= keep_near_extremes) : (separation >= find_separation && near >= find_near_extremes);
  }

  if (_seen) {
    _seen = blinking;
    if (!_seen) _seen_since.reset();
  } else {
    if (!blinking) _finding_since.reset();
    else if (!_finding_since) _finding_since = t;
    if (_finding_since && t - *_finding_since >= find_hold_ms) {
      _seen = true;
      _seen_since = *_finding_since - window_ms;
      _finding_since.reset();
    }
  }
  return _seen;
}

}  // namespace rpiledconfig::reader
