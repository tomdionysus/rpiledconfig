//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "tracker.h"

#include <algorithm>
#include <cmath>

namespace rpiledconfig::reader {

namespace {

// Kept per tile: two frame periods and a margin at 60 fps.
constexpr double history_seconds = 20;
constexpr double max_fps = 60;
// A tile read is given up for another only after the LED has been gone from it this long.
constexpr double switch_after_ms = 1000;

}  // namespace

Tracker::Tracker(TrackerConfig config) : _config(config) {}

void Tracker::_resize(int columns, int rows) {
  if (columns == _columns && rows == _rows) return;
  _columns = columns;
  _rows = rows;
  const auto tiles = static_cast<std::size_t>(columns * rows);
  _values.assign(tiles, 0.0);
  _top.assign(tiles, {});
  _detectors.assign(tiles, Detector(DetectorConfig{_config.symbol}));
  _capacity = static_cast<std::size_t>(history_seconds * max_fps);
  _count = 0;
  _head = 0;
  _times.assign(_capacity, 0.0);
  _history.assign(_capacity * tiles, 0.0f);
  _chosen.reset();
  _reader.reset();
  _state = TrackerState{};
}

void Tracker::_measure(const Plane& plane) {
  const int tile = std::max(1, _config.tile);
  const int step = std::max(1, _config.step);
  for (auto& top : _top) top = {0, 0, 0, 0};
  for (int y = 0; y < plane.height; y += step) {
    const std::uint8_t* row = plane.data + static_cast<std::ptrdiff_t>(y) * plane.row_stride;
    auto* tops = &_top[static_cast<std::size_t>((y / tile) * _columns)];
    for (int x = 0; x < plane.width; x += step) {
      const std::uint8_t v = row[static_cast<std::ptrdiff_t>(x) * plane.pixel_stride];
      auto& top = tops[x / tile];
      // The four brightest, smallest first.
      if (v <= top[0]) continue;
      top[0] = v;
      for (int i = 0; i < 3 && top[static_cast<std::size_t>(i)] > top[static_cast<std::size_t>(i + 1)]; ++i) std::swap(top[static_cast<std::size_t>(i)], top[static_cast<std::size_t>(i + 1)]);
    }
  }
  for (std::size_t i = 0; i < _top.size(); ++i) _values[i] = (_top[i][0] + _top[i][1] + _top[i][2] + _top[i][3]) / 4.0;
}

const TrackerState& Tracker::push(double t, const Plane& plane) {
  const int tile = std::max(1, _config.tile);
  _resize((plane.width + tile - 1) / tile, (plane.height + tile - 1) / tile);
  _state.plane_width = plane.width;
  _state.plane_height = plane.height;
  _measure(plane);
  return _update(t);
}

const TrackerState& Tracker::push_values(double t, int columns, int rows, const std::vector<double>& values) {
  _resize(columns, rows);
  _state.plane_width = columns * _config.tile;
  _state.plane_height = rows * _config.tile;
  _values = values;
  return _update(t);
}

const TrackerState& Tracker::_update(double t) {
  const std::size_t tiles = _values.size();
  if (tiles == 0) return _state;

  // History first, so a tile chosen now is given this frame too.
  _times[_head] = t;
  for (std::size_t i = 0; i < tiles; ++i) _history[i * _capacity + _head] = static_cast<float>(_values[i]);
  _head = (_head + 1) % _capacity;
  _count = std::min(_count + 1, _capacity);

  int candidates = 0;
  std::optional<int> best;
  double best_contrast = 0;
  for (std::size_t i = 0; i < tiles; ++i) {
    if (!_detectors[i].push(t, _values[i])) continue;
    ++candidates;
    if (_detectors[i].contrast() > best_contrast) {
      best_contrast = _detectors[i].contrast();
      best = static_cast<int>(i);
    }
  }
  _state.candidates = candidates;

  // Keep reading the chosen tile while the LED is there, or briefly gone; otherwise take the
  // best tile the LED is seen in now.
  if (_chosen) {
    const bool there = _detectors[static_cast<std::size_t>(*_chosen)].seen();
    if (there) {
      _chosen_lost_since = -1;
    } else if (_chosen_lost_since < 0) {
      _chosen_lost_since = t;
    }
    if (!there && best && *best != *_chosen && t - _chosen_lost_since >= switch_after_ms) {
      _chosen.reset();
      _reader.reset();
    }
  }
  if (!_chosen && best) {
    _chosen = best;
    _chosen_lost_since = -1;
    _reader = std::make_unique<Reader>(ReaderConfig{_config.symbol});
    // Its history up to the frame before this one; this frame follows below.
    const std::size_t oldest = (_head + _capacity - _count) % _capacity;
    for (std::size_t k = 0; k + 1 < _count; ++k) {
      const std::size_t at = (oldest + k) % _capacity;
      _reader->push(_times[at], _history[static_cast<std::size_t>(*_chosen) * _capacity + at]);
    }
  }

  if (_chosen && _reader) {
    const auto index = static_cast<std::size_t>(*_chosen);
    _state.reader = _reader->push(t, _values[index]);
    _state.sample = _values[index];
    const int column = *_chosen % _columns;
    const int row = *_chosen / _columns;
    _state.tile = TileRect{column * _config.tile, row * _config.tile, _config.tile, _config.tile};
  } else {
    _state.reader = ReaderState{};
    _state.tile.reset();
    _state.sample = _values[static_cast<std::size_t>((_rows / 2) * _columns + _columns / 2)];
  }
  return _state;
}

}  // namespace rpiledconfig::reader
