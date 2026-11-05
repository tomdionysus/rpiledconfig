//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "codec.h"

#include <algorithm>

namespace rpiledconfig::reader {

namespace {

struct Tables {
  std::array<std::uint8_t, 512> exp{};
  std::array<int, 256> log{};

  Tables() {
    int x = 1;
    for (int i = 0; i < 255; ++i) {
      exp[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(x);
      log[static_cast<std::size_t>(x)] = i;
      x <<= 1;
      if (x & 0x100) x ^= 0x11D;
    }
    // Doubled, so a product's log sum needs no reduction.
    for (int i = 255; i < 512; ++i) exp[static_cast<std::size_t>(i)] = exp[static_cast<std::size_t>(i - 255)];
  }
};

const Tables& tables() {
  static const Tables t;
  return t;
}

std::uint8_t mul(std::uint8_t a, std::uint8_t b) {
  if (a == 0 || b == 0) return 0;
  const auto& t = tables();
  return t.exp[static_cast<std::size_t>(t.log[a] + t.log[b])];
}

std::uint8_t div(std::uint8_t a, std::uint8_t b) {
  if (a == 0 || b == 0) return 0;
  const auto& t = tables();
  return t.exp[static_cast<std::size_t>((t.log[a] + 255 - t.log[b]) % 255)];
}

std::uint8_t alpha_pow(int p) { return tables().exp[static_cast<std::size_t>(((p % 255) + 255) % 255)]; }

std::uint8_t inverse(std::uint8_t a) { return div(1, a); }

// Horner's rule, highest power first.
std::uint8_t eval(const Bytes& p, std::uint8_t x) {
  std::uint8_t y = p.empty() ? 0 : p[0];
  for (std::size_t i = 1; i < p.size(); ++i) y = static_cast<std::uint8_t>(mul(y, x) ^ p[i]);
  return y;
}

Bytes syndromes(const Bytes& word) {
  Bytes out;
  for (std::size_t j = 0; j < parity_length; ++j) out.push_back(eval(word, alpha_pow(static_cast<int>(j))));
  return out;
}

// Position p in the word is the coefficient of x^(n-1-p); its locator is a^(n-1-p).
std::uint8_t locator_of(int position, int n) { return alpha_pow(n - 1 - position); }

// Berlekamp-Massey over `synd`, the locator lowest power first, or nullopt when it implies
// more errors than `capacity` syndromes can find.
std::optional<Bytes> error_locator(const Bytes& synd, int capacity) {
  Bytes lambda{1};
  Bytes prev{1};
  int l = 0;
  int m = 1;
  std::uint8_t b = 1;
  for (int k = 0; k < static_cast<int>(synd.size()); ++k) {
    std::uint8_t delta = synd[static_cast<std::size_t>(k)];
    for (int i = 1; i <= l; ++i) {
      const std::uint8_t li = static_cast<std::size_t>(i) < lambda.size() ? lambda[static_cast<std::size_t>(i)] : 0;
      delta ^= mul(li, synd[static_cast<std::size_t>(k - i)]);
    }
    if (delta == 0) {
      ++m;
      continue;
    }
    const std::uint8_t scale = div(delta, b);
    Bytes next = lambda;
    for (std::size_t i = 0; i < prev.size(); ++i) {
      const std::size_t at = i + static_cast<std::size_t>(m);
      if (next.size() <= at) next.resize(at + 1, 0);
      next[at] ^= mul(scale, prev[i]);
    }
    if (2 * l <= k) {
      prev = lambda;
      l = k + 1 - l;
      b = delta;
      m = 1;
    } else {
      ++m;
    }
    lambda = next;
  }
  while (lambda.size() > 1 && lambda.back() == 0) lambda.pop_back();
  const int errors = static_cast<int>(lambda.size()) - 1;
  if (errors != l || 2 * errors > capacity) return std::nullopt;
  return lambda;
}

}  // namespace

std::uint16_t crc16(const std::uint8_t* data, std::size_t length) {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < length; ++i) {
    crc = static_cast<std::uint16_t>(crc ^ (data[i] << 8));
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021) : static_cast<std::uint16_t>(crc << 1);
  }
  return crc;
}

Bytes rs_generator(std::size_t n) {
  Bytes g{1};
  for (std::size_t i = 0; i < n; ++i) {
    Bytes next(g.size() + 1, 0);
    const std::uint8_t root = alpha_pow(static_cast<int>(i));
    for (std::size_t j = 0; j < g.size(); ++j) {
      next[j] ^= g[j];
      next[j + 1] ^= mul(g[j], root);
    }
    g = next;
  }
  return g;
}

Bytes rs_parity(const Bytes& message, std::size_t n) {
  const Bytes g = rs_generator(n);
  Bytes rem(message);
  rem.resize(message.size() + n, 0);
  for (std::size_t i = 0; i < message.size(); ++i) {
    const std::uint8_t c = rem[i];
    if (c == 0) continue;
    for (std::size_t j = 1; j < g.size(); ++j) rem[i + j] ^= mul(g[j], c);
  }
  return Bytes(rem.begin() + static_cast<std::ptrdiff_t>(message.size()), rem.end());
}

std::optional<RsResult> rs_decode(const Bytes& received, const std::vector<int>& erased) {
  const int n = static_cast<int>(received.size());
  std::vector<int> erase_pos;
  for (const int p : erased) {
    if (p >= 0 && p < n && std::find(erase_pos.begin(), erase_pos.end(), p) == erase_pos.end()) erase_pos.push_back(p);
  }
  std::sort(erase_pos.begin(), erase_pos.end());
  if (erase_pos.size() > parity_length) return std::nullopt;

  Bytes word = received;
  for (const int p : erase_pos) word[static_cast<std::size_t>(p)] = 0;
  const Bytes synd = syndromes(word);
  if (std::all_of(synd.begin(), synd.end(), [](std::uint8_t s) { return s == 0; })) {
    return RsResult{word, 0, static_cast<int>(erase_pos.size())};
  }

  // The syndromes with the erasures' contribution removed, then Berlekamp-Massey over what
  // is left.
  Bytes fsynd = synd;
  for (const int p : erase_pos) {
    const std::uint8_t x = locator_of(p, n);
    for (std::size_t j = fsynd.size() - 1; j > 0; --j) fsynd[j] = static_cast<std::uint8_t>(mul(fsynd[j - 1], x) ^ fsynd[j]);
  }
  fsynd.erase(fsynd.begin(), fsynd.begin() + static_cast<std::ptrdiff_t>(erase_pos.size()));

  const auto lambda = error_locator(fsynd, static_cast<int>(parity_length - erase_pos.size()));
  if (!lambda) return std::nullopt;

  // Chien search: the positions whose inverse locator is a root of lambda.
  std::vector<int> err_pos;
  const int degree = static_cast<int>(lambda->size()) - 1;
  if (degree > 0) {
    const Bytes high_first(lambda->rbegin(), lambda->rend());
    for (int p = 0; p < n; ++p) {
      if (eval(high_first, inverse(locator_of(p, n))) == 0) err_pos.push_back(p);
    }
    if (static_cast<int>(err_pos.size()) != degree) return std::nullopt;
  }
  for (const int p : err_pos) {
    if (std::find(erase_pos.begin(), erase_pos.end(), p) != erase_pos.end()) return std::nullopt;
  }
  if (2 * err_pos.size() + erase_pos.size() > parity_length) return std::nullopt;

  // Forney's formula for every errata position at once, from the full syndromes.
  std::vector<int> positions = erase_pos;
  positions.insert(positions.end(), err_pos.begin(), err_pos.end());
  std::vector<std::uint8_t> x;
  for (const int p : positions) x.push_back(locator_of(p, n));
  Bytes loc{1};
  for (const auto xi : x) {
    Bytes next(loc.size() + 1, 0);
    for (std::size_t i = 0; i < loc.size(); ++i) {
      next[i] ^= loc[i];
      next[i + 1] ^= mul(loc[i], xi);
    }
    loc = next;
  }
  Bytes omega(parity_length, 0);
  for (std::size_t i = 0; i < parity_length; ++i) {
    for (std::size_t j = 0; j < loc.size() && i + j < parity_length; ++j) omega[i + j] ^= mul(synd[i], loc[j]);
  }
  const Bytes omega_high(omega.rbegin(), omega.rend());
  for (std::size_t i = 0; i < x.size(); ++i) {
    const std::uint8_t xi_inv = inverse(x[i]);
    std::uint8_t denom = 1;
    for (std::size_t j = 0; j < x.size(); ++j) {
      if (j != i) denom = mul(denom, static_cast<std::uint8_t>(1 ^ mul(xi_inv, x[j])));
    }
    if (denom == 0) return std::nullopt;
    // With the first root at a^0 the magnitude is Omega(X^-1) over the product.
    word[static_cast<std::size_t>(positions[i])] ^= div(eval(omega_high, xi_inv), denom);
  }

  const Bytes check = syndromes(word);
  if (!std::all_of(check.begin(), check.end(), [](std::uint8_t s) { return s == 0; })) return std::nullopt;
  return RsResult{word, static_cast<int>(err_pos.size()), static_cast<int>(erase_pos.size())};
}

Bytes message_for(const Id& id) {
  Bytes checked{frame_version};
  checked.insert(checked.end(), id.begin(), id.end());
  const std::uint16_t crc = crc16(checked.data(), checked.size());
  Bytes message(id.begin(), id.end());
  message.push_back(static_cast<std::uint8_t>(crc >> 8));
  message.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  return message;
}

Bytes codeword_for(const Id& id) {
  Bytes codeword = message_for(id);
  const Bytes parity = rs_parity(codeword, parity_length);
  codeword.insert(codeword.end(), parity.begin(), parity.end());
  return codeword;
}

std::vector<std::uint8_t> frame_symbols_for(const Id& id) {
  std::vector<std::uint8_t> symbols(sync_on, 1);
  symbols.insert(symbols.end(), sync_off, 0);
  for (const auto byte : codeword_for(id)) {
    for (const int nibble : {byte >> 4, byte & 0x0F}) {
      const auto word = words[static_cast<std::size_t>(nibble)];
      for (int s = static_cast<int>(word_symbols) - 1; s >= 0; --s) symbols.push_back(static_cast<std::uint8_t>((word >> s) & 1));
    }
  }
  return symbols;
}

std::optional<Read> read_codeword(const Bytes& received, const std::vector<int>& erased) {
  if (received.size() != codeword_length) return std::nullopt;
  const auto decoded = rs_decode(received, erased);
  if (!decoded) return std::nullopt;
  Id id{};
  std::copy(decoded->codeword.begin(), decoded->codeword.begin() + id_length, id.begin());
  const Bytes expected = message_for(id);
  if (!std::equal(expected.begin(), expected.end(), decoded->codeword.begin())) return std::nullopt;
  return Read{id, decoded->errors, decoded->erasures};
}

}  // namespace rpiledconfig::reader
