//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "tracker.h"

#include <algorithm>
#include <cmath>

namespace rpiledconfig::reader {

namespace {

// Kept per tile: two frame periods and a margin at 60 fps, at whatever rate this is.
constexpr double history_frames = 2.4;
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
  _capacity = static_cast<std::size_t>(history_frames * static_cast<double>(frame_symbols) * _config.symbol / 1000 * max_fps);
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
  _update(t);
  _locate(plane);
  return _state;
}

void Tracker::_locate(const Plane& plane) {
  if (!_state.tile) {
    _state.spot.reset();
    _dark_tile.reset();
    return;
  }

  // The tile and half a tile around it: an LED on a tile's edge is still found whole.
  const int step = std::max(1, _config.step);
  const int margin = _state.tile->width / 2;
  const int x0 = std::max(0, _state.tile->x - margin);
  const int y0 = std::max(0, _state.tile->y - margin);
  const int x1 = std::min(plane.width, _state.tile->x + _state.tile->width + margin);
  const int y1 = std::min(plane.height, _state.tile->y + _state.tile->height + margin);
  const int columns = (x1 - x0 + step - 1) / step;
  const int rows = (y1 - y0 + step - 1) / step;
  if (columns <= 0 || rows <= 0) return;

  const auto sample = [&](int column, int row) { return plane.data[static_cast<std::ptrdiff_t>(y0 + row * step) * plane.row_stride + static_cast<std::ptrdiff_t>(x0 + column * step) * plane.pixel_stride]; };

  if (_dark_tile != _chosen) {
    _state.spot.reset();
    _dark_tile.reset();
  }

  if (!_state.reader.level) {
    _dark.resize(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) _dark[static_cast<std::size_t>(row * columns + column)] = sample(column, row);
    }
    _dark_tile = _chosen;
    _dark_x = x0;
    _dark_y = y0;
    _dark_columns = columns;
    _dark_rows = rows;
    return;
  }

  if (!_dark_tile || _dark_x != x0 || _dark_y != y0 || _dark_columns != columns || _dark_rows != rows) return;

  // What the LED changed, not what is brightest: a lamp beside it is bright in both.
  int largest = 0;
  int peak_column = 0;
  int peak_row = 0;
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const int change = sample(column, row) - _dark[static_cast<std::size_t>(row * columns + column)];
      if (change > largest) {
        largest = change;
        peak_column = column;
        peak_row = row;
      }
    }
  }
  constexpr int least_change = 16;
  if (largest < least_change) return;

  // Only the core around the peak: a lens's reflections of the LED blink with it, and on the A85
  // they pulled a wider measure 25 pixels off the LED.
  const int threshold = largest * 3 / 4;
  const int reach = _state.tile->width / 2 / step;
  double weight = 0;
  double sum_x = 0;
  double sum_y = 0;
  for (int row = std::max(0, peak_row - reach); row <= std::min(rows - 1, peak_row + reach); ++row) {
    for (int column = std::max(0, peak_column - reach); column <= std::min(columns - 1, peak_column + reach); ++column) {
      const int change = sample(column, row) - _dark[static_cast<std::size_t>(row * columns + column)];
      if (change < threshold) continue;
      weight += change;
      sum_x += change * (x0 + column * step);
      sum_y += change * (y0 + row * step);
    }
  }

  // Smoothed, so that the box does not shake with the picture's noise.
  constexpr double follow = 0.3;
  const Spot found{sum_x / weight, sum_y / weight};
  _state.spot = _state.spot ? Spot{_state.spot->x + follow * (found.x - _state.spot->x), _state.spot->y + follow * (found.y - _state.spot->y)} : found;
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
