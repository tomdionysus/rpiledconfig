//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "types/status.h"

namespace rpiledconfig::protocol::rs {

// Reed-Solomon over GF(2^8) as docs/protocol.md has it for the LED frame: the primitive
// polynomial 0x11D, a = 2, the generator (x - a^0)(x - a^1)...(x - a^(n-1)) for n parity
// bytes, systematic with the message first. Written out here rather than taken from a
// library; the daemon only encodes, and the decoder is the
// app's.

// Polynomials are byte vectors, highest power first, as the protocol's vectors are written.

// Multiplication in GF(2^8) mod 0x11D. Addition is XOR.
std::uint8_t multiply(std::uint8_t a, std::uint8_t b);

// a^power, power taken mod 255.
std::uint8_t power_of_a(unsigned power);

// The generator for this many parity bytes: monic, parity + 1 coefficients.
types::Bytes generator(std::size_t parity);

// The parity bytes for a message: the remainder of message(x) * x^parity divided by the
// generator. The codeword is the message followed by these.
types::Bytes parity(const types::Bytes& message, std::size_t parity);

// The codeword evaluated at a^0 .. a^(count - 1), with its first byte the highest power:
// every one is zero for a valid codeword.
types::Bytes syndromes(const types::Bytes& codeword, std::size_t count);

}  // namespace rpiledconfig::protocol::rs
