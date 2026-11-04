//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "reader.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rpiledconfig::reader {

namespace {

constexpr int frame_t = static_cast<int>(frame_symbols);
// Folding: bins per symbol, the bins left out at each edge of a symbol (where bloom and blur
// put light that belongs to the neighbour), and bins in a cycle.
constexpr int bins_per_symbol = 10;
constexpr int edge_bins = 1;
constexpr int fold_bins = frame_t * bins_per_symbol;
// The coarse search: rates 0.25% apart across the tolerance (at most 0.125% off, two syncs
// a frame apart fold within a quarter symbol of each other), starts every 2 bins.
constexpr double coarse_rate_step = 0.0025;
constexpr int coarse_offset_step = 2;
// Leaders refined, alignments read out, and the fine search around each leader.
constexpr int refine_top = 3;
constexpr int read_top = 3;
constexpr double fine_rate_span = 0.006;
constexpr double fine_rate_step = 0.001;
constexpr int fine_shift_bins = 6;
// A word whose best match beats the next by less than this, in typical symbol strengths,
// is too uncertain to guess: its byte is erased.
constexpr double erase_below = 0.3;
// Matching starts once this share of a frame is held: a read needs 10 of the 16 bytes and
// the sync, about 65% of a frame. Then it is repeated this often.
constexpr double first_try_share = 0.6;
constexpr double try_every_ms = 250;

double frame_ms(double symbol) { return frame_t * symbol; }

// The samples folded onto one cycle at `symbol` from `origin`, as a running total over bins
// of a tenth of a symbol: entry b is the sum of the bins before b.
void fold(const std::vector<double>& t, const std::vector<double>& d, double origin, double symbol, std::vector<double>& totals) {
  totals.assign(fold_bins + 1, 0.0);
  for (std::size_t i = 0; i < t.size(); ++i) {
    double position = (t[i] - origin) / symbol;
    position -= std::floor(position / frame_t) * frame_t;
    int bin = static_cast<int>(position * bins_per_symbol);
    if (bin >= fold_bins) bin = fold_bins - 1;
    if (bin < 0) bin = 0;
    totals[static_cast<std::size_t>(bin + 1)] += d[i];
  }
  for (int b = 1; b <= fold_bins; ++b) totals[static_cast<std::size_t>(b)] += totals[static_cast<std::size_t>(b - 1)];
}

// Each symbol's sum over its interior bins, for symbols starting `offset` bins into the
// fold; twice over, so a run from any symbol needs no wrapping.
void symbol_sums(const std::vector<double>& totals, int offset, std::vector<double>& sums) {
  sums.assign(2 * frame_t, 0.0);
  constexpr int inner = bins_per_symbol - 2 * edge_bins;
  for (int h = 0; h < frame_t; ++h) {
    const int from = offset + h * bins_per_symbol + edge_bins;
    const int to = from + inner;
    const double sum = to <= fold_bins ? totals[static_cast<std::size_t>(to)] - totals[static_cast<std::size_t>(from)]
                                       : totals[fold_bins] - totals[static_cast<std::size_t>(from)] + totals[static_cast<std::size_t>(to - fold_bins)];
    sums[static_cast<std::size_t>(h)] = sum;
    sums[static_cast<std::size_t>(h + frame_t)] = sum;
  }
}

// The score of a frame starting at symbol j: the sync agreeing plus every data symbol's
// decisiveness. `run` holds running totals of the sums and `strength` of their magnitudes.
double score_at(const std::vector<double>& run, const std::vector<double>& strength, int j) {
  const auto at = [&](const std::vector<double>& v, int i) { return v[static_cast<std::size_t>(i)]; };
  const double on = at(run, j + static_cast<int>(sync_on)) - at(run, j);
  const double off = at(run, j + static_cast<int>(sync_symbols)) - at(run, j + static_cast<int>(sync_on));
  const double data = at(strength, j + frame_t) - at(strength, j + static_cast<int>(sync_symbols));
  return on - off + data;
}

void running(const std::vector<double>& sums, std::vector<double>& run, std::vector<double>& strength) {
  run.assign(sums.size() + 1, 0.0);
  strength.assign(sums.size() + 1, 0.0);
  for (std::size_t h = 0; h < sums.size(); ++h) {
    run[h + 1] = run[h] + sums[h];
    strength[h + 1] = strength[h] + std::abs(sums[h]);
  }
}

double cyclic_gap(double a, double b, double period) {
  double d = std::fmod(a - b, period);
  if (d < 0) d += period;
  return std::min(d, period - d);
}

}  // namespace

Reader::Reader(ReaderConfig config) : _config(config), _detector(DetectorConfig{config.symbol}) {}

void Reader::_clear() {
  _samples.clear();
  _started = false;
  _last_try = -1e18;
  _slots.fill(0);
  _slots_seen = 0;
  _state.coverage = 0;
  _state.erasures = static_cast<int>(codeword_length);
}

void Reader::_mark(double t) {
  int slot = static_cast<int>(std::floor(t / _config.symbol)) % frame_t;
  if (slot < 0) slot += frame_t;
  if (!_slots[static_cast<std::size_t>(slot)]) {
    _slots[static_cast<std::size_t>(slot)] = 1;
    ++_slots_seen;
  }
}

const ReaderState& Reader::push(double t, double y) {
  const bool was_seen = _detector.seen();
  _detector.push(t, y);
  _state.contrast = _detector.contrast();
  _state.level = _detector.level();
  if (_detector.went_dark()) _clear();
  if (_detector.contrast() <= 0 || (!_started && _detector.contrast() < 10)) {
    _state.led_seen = false;
    return _state;
  }
  if (!_started) {
    _started = true;
    _first_t = t;
  }

  const bool seen = _detector.seen();
  _state.led_seen = seen;
  if (seen && !was_seen) {
    // The windows that showed it is the LED were the LED: their samples are code seen.
    const double from = _detector.seen_since().value_or(t);
    for (const auto& s : _samples) {
      if (s.t >= from) _mark(s.t);
    }
  }
  if (!seen && _slots_seen > 0) {
    // Lost: what was seen may have been a different light or a moved phone.
    _slots.fill(0);
    _slots_seen = 0;
  }
  if (seen) _mark(t);
  _state.coverage = static_cast<double>(_slots_seen) / frame_t;

  _samples.push_back(Sample{t, y - _detector.middle()});
  const double keep = 2 * frame_ms(_config.symbol) * (1 + clock_tolerance);
  while (!_samples.empty() && _samples.front().t < t - keep) _samples.pop_front();

  const double span = t - _samples.front().t;
  if (!_state.result && span >= first_try_share * frame_ms(_config.symbol) && t - _last_try >= try_every_ms) {
    _last_try = t;
    const int failed_before = static_cast<int>(std::floor((t - _first_t) / frame_ms(_config.symbol)));
    if (!try_now()) _state.frames_failed = std::max(_state.frames_failed, failed_before);
  }
  return _state;
}

bool Reader::try_now() {
  if (_samples.empty()) return false;
  ++_state.tries;
  const std::vector<Sample> all(_samples.begin(), _samples.end());
  // Where the frame sits is a property of the LED, not of the window: found once, on
  // everything held, where the most cycles agree.
  const auto candidates = _align(all);
  if (candidates.empty()) return false;

  // The latest frame period alone, so older samples from a bad moment cannot hold a read
  // back, and everything held, so two partial looks can fill each other in.
  const double last = all.back().t;
  std::vector<Sample> latest;
  for (const auto& s : all) {
    if (s.t >= last - frame_ms(_config.symbol) * (1 + clock_tolerance)) latest.push_back(s);
  }
  int fewest = static_cast<int>(codeword_length);
  if (latest.size() < all.size() && _read_window(latest, candidates, fewest)) return true;
  if (_read_window(all, candidates, fewest)) return true;
  _state.erasures = fewest;
  return false;
}

std::vector<Reader::Alignment> Reader::_align(const std::vector<Sample>& samples) const {
  std::vector<double> t;
  std::vector<double> d;
  t.reserve(samples.size());
  d.reserve(samples.size());
  for (const auto& s : samples) {
    t.push_back(s.t);
    d.push_back(s.d);
  }
  const double origin = t.front();

  // The best rate and offset for each symbol the frame could start at.
  std::array<double, frame_symbols> best_score;
  best_score.fill(-std::numeric_limits<double>::infinity());
  std::array<double, frame_symbols> best_symbol{};
  std::array<int, frame_symbols> best_offset{};
  std::vector<double> totals;
  std::vector<double> sums;
  std::vector<double> run;
  std::vector<double> strength;
  const int rates = static_cast<int>(std::lround(2 * clock_tolerance / coarse_rate_step));
  for (int r = 0; r <= rates; ++r) {
    const double symbol = _config.symbol * (1 - clock_tolerance + r * coarse_rate_step);
    fold(t, d, origin, symbol, totals);
    for (int offset = 0; offset < bins_per_symbol; offset += coarse_offset_step) {
      symbol_sums(totals, offset, sums);
      running(sums, run, strength);
      for (int j = 0; j < frame_t; ++j) {
        const double score = score_at(run, strength, j);
        if (score <= best_score[static_cast<std::size_t>(j)]) continue;
        best_score[static_cast<std::size_t>(j)] = score;
        best_symbol[static_cast<std::size_t>(j)] = symbol;
        best_offset[static_cast<std::size_t>(j)] = offset;
      }
    }
  }
  std::vector<Alignment> coarse;
  for (int j = 0; j < frame_t; ++j) {
    const auto s = static_cast<std::size_t>(j);
    coarse.push_back(Alignment{origin + (static_cast<double>(best_offset[s]) / bins_per_symbol + j) * best_symbol[s], best_symbol[s], best_score[s]});
  }
  std::sort(coarse.begin(), coarse.end(), [](const Alignment& a, const Alignment& b) { return a.score > b.score; });

  // Leaders a few symbols apart, not the neighbours of one peak.
  std::vector<Alignment> leaders;
  for (const auto& c : coarse) {
    if (static_cast<int>(leaders.size()) >= refine_top) break;
    bool near = false;
    for (const auto& l : leaders) near = near || cyclic_gap(l.start, c.start, frame_t * c.symbol) < 3 * c.symbol;
    if (!near) leaders.push_back(c);
  }

  // The fine search: rate either way in small steps, carried to each rate at the window's
  // middle, where a rate change moves the start least, and start a few bins either way.
  const double middle = (t.front() + t.back()) / 2;
  std::vector<Alignment> refined;
  for (const auto& lead : leaders) {
    Alignment best = lead;
    const int steps = static_cast<int>(std::lround(fine_rate_span / fine_rate_step));
    for (int i = -steps; i <= steps; ++i) {
      const double symbol = lead.symbol * (1 + i * fine_rate_step);
      if (std::abs(symbol / _config.symbol - 1) > clock_tolerance + 1e-9) continue;
      const double cycles = std::round((middle - lead.start) / (frame_t * lead.symbol));
      const double anchored = middle - (middle - lead.start - cycles * frame_t * lead.symbol) * (symbol / lead.symbol) - cycles * frame_t * symbol;
      fold(t, d, anchored, symbol, totals);
      for (int shift = -fine_shift_bins; shift <= fine_shift_bins; ++shift) {
        const int bin = ((shift % fold_bins) + fold_bins) % fold_bins;
        symbol_sums(totals, bin % bins_per_symbol, sums);
        running(sums, run, strength);
        const double score = score_at(run, strength, bin / bins_per_symbol);
        if (score > best.score) best = Alignment{anchored + static_cast<double>(shift) / bins_per_symbol * symbol, symbol, score};
      }
    }
    refined.push_back(best);
  }
  std::sort(refined.begin(), refined.end(), [](const Alignment& a, const Alignment& b) { return a.score > b.score; });
  return refined;
}

bool Reader::_read_window(const std::vector<Sample>& samples, const std::vector<Alignment>& candidates, int& fewest) {
  for (std::size_t c = 0; c < candidates.size() && static_cast<int>(c) < read_top; ++c) {
    const auto& a = candidates[c];
    // Each symbol's evidence for on, every cycle's samples together, weighted towards the
    // middle of the symbol, where neither neighbour's light reaches.
    std::array<double, frame_symbols> value{};
    std::array<std::uint8_t, frame_symbols> seen{};
    for (const auto& s : samples) {
      double position = (s.t - a.start) / a.symbol;
      position -= std::floor(position / frame_t) * frame_t;
      const int h = std::min(frame_t - 1, static_cast<int>(position));
      const double weight = std::sin(3.14159265358979 * (position - h));
      value[static_cast<std::size_t>(h)] += s.d * weight;
      seen[static_cast<std::size_t>(h)] = 1;
    }
    std::vector<double> magnitudes;
    for (std::size_t h = sync_symbols; h < frame_symbols; ++h) {
      if (seen[h]) magnitudes.push_back(std::abs(value[h]));
    }
    if (magnitudes.empty()) continue;
    std::nth_element(magnitudes.begin(), magnitudes.begin() + static_cast<std::ptrdiff_t>(magnitudes.size() / 2), magnitudes.end());
    const double typical = magnitudes[magnitudes.size() / 2];
    if (typical <= 0) continue;

    Bytes bytes(codeword_length, 0);
    std::vector<int> erased;
    for (std::size_t byte = 0; byte < codeword_length; ++byte) {
      bool doubtful = false;
      int nibbles[2] = {0, 0};
      for (int half = 0; half < 2; ++half) {
        const std::size_t first = sync_symbols + (2 * byte + static_cast<std::size_t>(half)) * word_symbols;
        bool all_seen = true;
        for (std::size_t i = 0; i < word_symbols; ++i) all_seen = all_seen && seen[first + i];
        double best = -std::numeric_limits<double>::infinity();
        double second = -std::numeric_limits<double>::infinity();
        int best_n = 0;
        for (int n = 0; n < 16; ++n) {
          double match = 0;
          for (std::size_t i = 0; i < word_symbols; ++i) {
            const bool on = ((words[static_cast<std::size_t>(n)] >> (word_symbols - 1 - i)) & 1) != 0;
            match += on ? value[first + i] : -value[first + i];
          }
          if (match > best) {
            second = best;
            best = match;
            best_n = n;
          } else if (match > second) {
            second = match;
          }
        }
        nibbles[half] = best_n;
        if (!all_seen || (best - second) / (2 * typical) < erase_below) doubtful = true;
      }
      bytes[byte] = static_cast<std::uint8_t>((nibbles[0] << 4) | nibbles[1]);
      if (doubtful) erased.push_back(static_cast<int>(byte));
    }
    fewest = std::min(fewest, static_cast<int>(erased.size()));
    if (erased.size() > parity_length) continue;
    const auto read = read_codeword(bytes, erased);
    if (!read) continue;
    _state.result = read;
    _state.erasures = read->erasures;
    const double span = samples.back().t - samples.front().t;
    _state.frames_combined = std::max(1, static_cast<int>(std::ceil(span / frame_ms(_config.symbol) - 1e-9)));
    return true;
  }
  return false;
}

}  // namespace rpiledconfig::reader
