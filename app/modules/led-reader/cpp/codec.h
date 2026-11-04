//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "protocol.h"

// The frame's codeword (docs/protocol.md, "The codeword"): CRC-16/CCITT-FALSE, RS(16, 10)
// over GF(2^8) with 0x11D and a = 2, and the encoder, which the tests and the synthetic
// camera use. Decoding corrects errors and erasures together, 2 x errors + erasures <= 6:
// syndromes, Forney syndromes to take the erasures out, Berlekamp-Massey for the error
// locator, Chien search for the positions, Forney's formula for the values. Written from
// the textbook algorithm, as app/src/led/rs.ts was before it; no library.
namespace rpiledconfig::reader {

using Bytes = std::vector<std::uint8_t>;
using Id = std::array<std::uint8_t, id_length>;

std::uint16_t crc16(const std::uint8_t* data, std::size_t length);

// g(x) = (x - a^0)...(x - a^(n-1)), highest power first.
Bytes rs_generator(std::size_t n);
// The parity of `message` under rs_generator(n).
Bytes rs_parity(const Bytes& message, std::size_t n);

struct RsResult {
  Bytes codeword;
  int errors = 0;
  int erasures = 0;
};

// Corrects `received` (codeword_length bytes) given erased positions; nullopt when the
// pattern is beyond the code.
std::optional<RsResult> rs_decode(const Bytes& received, const std::vector<int>& erased);

// docs/protocol.md: id_bytes and the CRC over the version byte and id_bytes.
Bytes message_for(const Id& id);
Bytes codeword_for(const Id& id);
// The 170 symbols of a frame, sync first: 1 on, 0 off.
std::vector<std::uint8_t> frame_symbols_for(const Id& id);

struct Read {
  Id id{};
  int errors = 0;
  int erasures = 0;
};

// The last step of a read: correct, then accept only a word whose CRC is right.
std::optional<Read> read_codeword(const Bytes& received, const std::vector<int>& erased);

}  // namespace rpiledconfig::reader
