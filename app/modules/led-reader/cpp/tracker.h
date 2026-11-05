//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "detector.h"
#include "reader.h"

// Finds the blinking LED anywhere in the camera's frames and reads it (docs/protocol.md,
// "Decoding"). Each frame's luminance plane is cut into square tiles; a tile's brightness is
// the mean of its brightest few sampled pixels, so a small LED is not averaged away in a lit
// room. Every tile has a Detector; the tile taken is the one the LED is found in with the
// greatest contrast, and a Reader is given that tile's history (kept for every tile, so
// nothing seen before the LED was found is lost) and then its every new sample.
namespace rpiledconfig::reader {

// One luminance plane: `width` x `height` pixels, rows `row_stride` bytes apart, pixels
// `pixel_stride` bytes apart (1 for a Y plane).
struct Plane {
  const std::uint8_t* data = nullptr;
  int width = 0;
  int height = 0;
  int row_stride = 0;
  int pixel_stride = 1;
};

struct TileRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Where the LED itself is, in the plane's pixels: the middle of what changes between its dark
// frames and its lit ones.
struct Spot {
  double x = 0;
  double y = 0;
};

struct TrackerConfig {
  // Tile side and pixel step, in pixels of the plane.
  int tile = 40;
  int step = 2;
  double symbol = symbol_ms;
};

struct TrackerState {
  ReaderState reader;
  // The tile being read, in the plane's pixels, and the plane's size.
  std::optional<TileRect> tile;
  // The LED within it, once it has been seen both lit and dark.
  std::optional<Spot> spot;
  int plane_width = 0;
  int plane_height = 0;
  // Tiles in which the LED is seen now.
  int candidates = 0;
  // This frame's brightness of the tile read, or of the centre tile when none is: what a
  // recording keeps.
  double sample = 0;
};

class Tracker {
 public:
  explicit Tracker(TrackerConfig config = {});

  const TrackerState& push(double t, const Plane& plane);

  // The same, from brightnesses already measured, one per tile in rows: for tests.
  const TrackerState& push_values(double t, int columns, int rows, const std::vector<double>& values);

  const TrackerState& state() const { return _state; }

 private:
  void _resize(int columns, int rows);
  void _measure(const Plane& plane);
  void _locate(const Plane& plane);
  const TrackerState& _update(double t);

  TrackerConfig _config;
  int _columns = 0;
  int _rows = 0;
  std::vector<double> _values;
  std::vector<std::array<std::uint8_t, 4>> _top;
  std::vector<Detector> _detectors;
  // The history of every tile, a ring of `_capacity` frames.
  std::size_t _capacity = 0;
  std::size_t _count = 0;
  std::size_t _head = 0;
  std::vector<double> _times;
  std::vector<float> _history;
  std::optional<int> _chosen;
  std::unique_ptr<Reader> _reader;
  double _chosen_lost_since = -1;
  // The search area's last dark frame, in rows of samples, and the tile it belongs to.
  std::vector<std::uint8_t> _dark;
  std::optional<int> _dark_tile;
  int _dark_x = 0;
  int _dark_y = 0;
  int _dark_columns = 0;
  int _dark_rows = 0;
  TrackerState _state;
};

}  // namespace rpiledconfig::reader
