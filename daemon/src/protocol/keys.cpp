//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "keys.h"

#include "crypto.h"
#include "spake2.h"
#include "util.h"

namespace rpiledconfig::protocol {

types::Bytes password_scalar(const machine_id::MachineId& id) { return spake2::password_scalar(id.id_bytes()); }

std::string fingerprint(const machine_id::MachineId& id) {
  const auto digest = crypto::sha256(id.id_bytes());
  if (digest.size() < 2) return {};
  return util::to_hex(digest.data(), 2);
}

types::Bytes session_key(const types::Bytes& ke) { return crypto::hkdf_sha256(ke, {}, util::to_bytes("session-key v2"), key_length); }

}  // namespace rpiledconfig::protocol
