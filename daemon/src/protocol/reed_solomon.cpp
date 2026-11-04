//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "reed_solomon.h"

#include <array>
#include <utility>

namespace rpiledconfig::protocol::rs {

namespace {

constexpr unsigned primitive = 0x11D;

// Logarithms and powers of a = 2. exp is doubled in length so that a product's exponent
// (two logarithms of up to 254 each) needs no reduction.
struct Tables {
  std::array<std::uint8_t, 512> exp{};
  std::array<unsigned, 256> log{};

  Tables() {
    unsigned x = 1;
    for (unsigned i = 0; i < 255; ++i) {
      exp[i] = static_cast<std::uint8_t>(x);
      log[x] = i;
      x <<= 1;
      if (x & 0x100) x ^= primitive;
    }
    for (unsigned i = 255; i < exp.size(); ++i) exp[i] = exp[i - 255];
  }
};

const Tables& tables() {
  static const Tables t;
  return t;
}

}  // namespace

std::uint8_t multiply(std::uint8_t a, std::uint8_t b) {
  if (a == 0 || b == 0) return 0;
  const auto& t = tables();
  return t.exp[t.log[a] + t.log[b]];
}

std::uint8_t power_of_a(unsigned power) { return tables().exp[power % 255]; }

types::Bytes generator(std::size_t parity) {
  types::Bytes g{1};

  // Times (x - a^i), which in GF(2^8) is (x + a^i): every coefficient moves up one power,
  // and the one below it, times a^i, is added in.
  for (std::size_t i = 0; i < parity; ++i) {
    const std::uint8_t root = power_of_a(static_cast<unsigned>(i));
    types::Bytes next(g.size() + 1, 0);
    for (std::size_t j = 0; j < g.size(); ++j) {
      next[j] ^= g[j];
      next[j + 1] ^= multiply(g[j], root);
    }
    g = std::move(next);
  }

  return g;
}

types::Bytes parity(const types::Bytes& message, std::size_t count) {
  const auto g = generator(count);

  // Long division of message(x) * x^count by the monic generator, keeping the remainder.
  types::Bytes work(message);
  work.resize(message.size() + count, 0);

  for (std::size_t i = 0; i < message.size(); ++i) {
    const std::uint8_t coefficient = work[i];
    if (coefficient == 0) continue;
    for (std::size_t j = 1; j < g.size(); ++j) work[i + j] ^= multiply(g[j], coefficient);
  }

  return types::Bytes(work.begin() + static_cast<std::ptrdiff_t>(message.size()), work.end());
}

types::Bytes syndromes(const types::Bytes& codeword, std::size_t count) {
  types::Bytes out(count, 0);

  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t x = power_of_a(static_cast<unsigned>(i));
    std::uint8_t value = 0;

    // Horner, first byte the highest power.
    for (const auto byte : codeword) value = static_cast<std::uint8_t>(multiply(value, x) ^ byte);
    out[i] = value;
  }

  return out;
}

}  // namespace rpiledconfig::protocol::rs
