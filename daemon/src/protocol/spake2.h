//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>

#include "types/status.h"

namespace rpiledconfig::protocol::spake2 {

// SPAKE2 of RFC 9382 with the ciphersuite SPAKE2-P256-SHA256-HKDF-HMAC, over OpenSSL's
// EC_POINT arithmetic. Both roles are here: the daemon is B, and the tests play A to talk
// to it. Scalars are 32 bytes big-endian; points are SEC1 uncompressed, 65 bytes.

using types::Bytes;

inline constexpr std::size_t scalar_length = 32;
inline constexpr std::size_t share_length = 65;
inline constexpr std::size_t confirmation_length = 32;

// A uses M, B uses N (RFC 9382, section 3.3).
enum class Role { a, b };

// w = HKDF(ikm = id_bytes, salt = "RPiLEDConfig", info = "spake2-w v2", L = 40) mod n,
// docs/protocol.md "Keys". Empty when OpenSSL failed.
Bytes password_scalar(const Bytes& id_bytes);

// A scalar drawn uniformly from [1, n) by rejection sampling. Empty when the CSPRNG
// failed, which the caller treats as fatal: a predictable x or y gives the password away.
Bytes random_scalar();

// pA = x*P + w*M for A, pB = y*P + w*N for B. Empty on a malformed scalar.
Bytes share(Role role, const Bytes& w, const Bytes& scalar);

struct Keys {
  Bytes k;    // the shared point, 65 bytes; never a key on its own
  Bytes tt;   // the transcript
  Bytes ke;   // the shared secret, 16 bytes
  Bytes kca;  // A's confirmation key, 16 bytes
  Bytes kcb;  // B's confirmation key, 16 bytes
  Bytes ca;   // HMAC-SHA256(KcA, TT)
  Bytes cb;   // HMAC-SHA256(KcB, TT)
};

// One side's view of a finished exchange, from its own scalar and both shares. Fails when
// the peer's share is not exactly 65 bytes, not on P-256, or the identity, or when K is
// the identity: RFC 9382, section 7, says to abort on all of these.
types::Result<Keys> finish(Role role, const Bytes& w, const Bytes& scalar, const Bytes& own_share, const Bytes& peer_share, const Bytes& a_identity,
                           const Bytes& b_identity, const Bytes& aad);

}  // namespace rpiledconfig::protocol::spake2
