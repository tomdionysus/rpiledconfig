//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include "spake2.h"
#include "types/status.h"

namespace rpiledconfig::protocol {

// docs/protocol.md, "Authentication": SPAKE2 with the app as A and the daemon as B, empty
// identities, and this AAD.
inline constexpr char auth_aad[] = "RPiLEDConfig auth v2";

// What the Auth characteristic takes: pA (65) || cA (32).
inline constexpr std::size_t auth_write_length = spake2::share_length + spake2::confirmation_length;

// What AuthResult holds.
inline constexpr std::uint8_t auth_accepted = 0x01;
inline constexpr std::uint8_t auth_refused = 0x00;

// The daemon's side of one Auth write, against the y and pB it handed out in Challenge.
// The keys when the write is well formed, pA is a valid point and cA verifies (constant
// time); otherwise why not, for the log.
types::Result<spake2::Keys> accept_auth(const types::Bytes& w, const types::Bytes& y, const types::Bytes& p_b, const types::Bytes& write);

// The app's side, for the tests and the vectors: the Auth write for pB, and the keys it
// expects the daemon to confirm with.
struct AppAuth {
  types::Bytes write;
  spake2::Keys keys;
};
types::Result<AppAuth> app_auth(const types::Bytes& w, const types::Bytes& x, const types::Bytes& p_b);

}  // namespace rpiledconfig::protocol
