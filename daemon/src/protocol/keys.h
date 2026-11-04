//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>

#include "machine_id/machine_id.h"
#include "types/status.h"

namespace rpiledconfig::protocol {

// The derivations of docs/protocol.md, "Keys".

inline constexpr std::size_t key_length = 32;

// w, the SPAKE2 password scalar: 32 bytes big-endian. Empty when OpenSSL failed.
types::Bytes password_scalar(const machine_id::MachineId& id);

// The first 2 bytes of SHA-256(id_bytes), as 4 lowercase hex characters: in the
// advertisement, in Info, and shown to the user.
std::string fingerprint(const machine_id::MachineId& id);

// S = HKDF(ikm = Ke, salt = empty, info = "session-key v2", L = 32).
types::Bytes session_key(const types::Bytes& ke);

}  // namespace rpiledconfig::protocol
