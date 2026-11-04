//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "auth.h"

#include "crypto.h"
#include "util.h"

namespace rpiledconfig::protocol {

types::Result<spake2::Keys> accept_auth(const types::Bytes& w, const types::Bytes& y, const types::Bytes& p_b, const types::Bytes& write) {
  using R = types::Result<spake2::Keys>;

  if (write.size() != auth_write_length)
    return R::failure("an Auth write of " + std::to_string(write.size()) + " bytes, not " + std::to_string(auth_write_length));

  const types::Bytes p_a(write.begin(), write.begin() + spake2::share_length);
  const types::Bytes c_a(write.begin() + spake2::share_length, write.end());

  auto keys = spake2::finish(spake2::Role::b, w, y, p_b, p_a, {}, {}, util::to_bytes(auth_aad));
  if (!keys) return R::failure(keys.error);

  if (!crypto::equal(keys.value.ca, c_a)) return R::failure("cA does not verify");

  return keys;
}

types::Result<AppAuth> app_auth(const types::Bytes& w, const types::Bytes& x, const types::Bytes& p_b) {
  using R = types::Result<AppAuth>;

  AppAuth out;
  const auto p_a = spake2::share(spake2::Role::a, w, x);
  if (p_a.empty()) return R::failure("cannot make pA");

  auto keys = spake2::finish(spake2::Role::a, w, x, p_a, p_b, {}, {}, util::to_bytes(auth_aad));
  if (!keys) return R::failure(keys.error);

  out.write = p_a;
  out.write.insert(out.write.end(), keys.value.ca.begin(), keys.value.ca.end());
  out.keys = std::move(keys.value);
  return R::success(std::move(out));
}

}  // namespace rpiledconfig::protocol
