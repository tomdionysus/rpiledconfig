//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "spake2.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <memory>

#include "crypto.h"
#include "util.h"

namespace rpiledconfig::protocol::spake2 {

namespace {

struct GroupDeleter {
  void operator()(EC_GROUP* group) const { EC_GROUP_free(group); }
};
struct PointDeleter {
  void operator()(EC_POINT* point) const { EC_POINT_free(point); }
};
struct BigDeleter {
  // Clear: these hold w, x and y.
  void operator()(BIGNUM* big) const { BN_clear_free(big); }
};
struct ContextDeleter {
  void operator()(BN_CTX* ctx) const { BN_CTX_free(ctx); }
};

using Group = std::unique_ptr<EC_GROUP, GroupDeleter>;
using Point = std::unique_ptr<EC_POINT, PointDeleter>;
using Big = std::unique_ptr<BIGNUM, BigDeleter>;
using Context = std::unique_ptr<BN_CTX, ContextDeleter>;

// RFC 9382, section 6, for P-256: compressed SEC1.
constexpr const char* m_hex = "02886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f";
constexpr const char* n_hex = "03d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49";

// Everything one computation needs, made together so that a failure anywhere is one
// check. OpenSSL's group objects are not shared across threads here, so each call makes
// its own; the cost is nothing next to the scalar multiplications.
struct Curve {
  Group group{EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1)};
  Context ctx{BN_CTX_new()};
  Point m;
  Point n;

  Curve() {
    if (!group || !ctx) return;
    m = decode(util::from_hex(m_hex).value_or(Bytes{}));
    n = decode(util::from_hex(n_hex).value_or(Bytes{}));
  }

  bool ok() const { return group && ctx && m && n; }

  const BIGNUM* order() const { return EC_GROUP_get0_order(group.get()); }

  Point make() const { return Point(EC_POINT_new(group.get())); }

  // Any SEC1 form OpenSSL takes; on-curve is checked by OpenSSL, and again here so that
  // the check does not depend on the version.
  Point decode(const Bytes& octets) const {
    Point point = make();
    if (!point) return {};
    if (EC_POINT_oct2point(group.get(), point.get(), octets.data(), octets.size(), ctx.get()) != 1) return {};
    if (EC_POINT_is_on_curve(group.get(), point.get(), ctx.get()) != 1) return {};
    if (EC_POINT_is_at_infinity(group.get(), point.get()) == 1) return {};
    return point;
  }

  Bytes encode(const EC_POINT* point) const {
    Bytes out(share_length);
    if (EC_POINT_point2oct(group.get(), point, POINT_CONVERSION_UNCOMPRESSED, out.data(), out.size(), ctx.get()) != share_length) return {};
    return out;
  }

  // A scalar in [1, n), marked constant-time; anything else is refused rather than reduced.
  Big scalar(const Bytes& bytes) const {
    if (bytes.size() != scalar_length) return {};
    Big big(BN_bin2bn(bytes.data(), static_cast<int>(bytes.size()), nullptr));
    if (!big) return {};
    BN_set_flags(big.get(), BN_FLG_CONSTTIME);
    if (BN_is_zero(big.get()) || BN_cmp(big.get(), order()) >= 0) return {};
    return big;
  }
};

Bytes scalar_bytes(const BIGNUM* big) {
  Bytes out(scalar_length);
  if (BN_bn2binpad(big, out.data(), static_cast<int>(out.size())) != static_cast<int>(scalar_length)) return {};
  return out;
}

// len(s) as RFC 9382 has it: eight bytes, little-endian, then s.
void append_with_length(Bytes& out, const Bytes& s) {
  std::uint64_t length = s.size();
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>(length & 0xFF));
    length >>= 8;
  }
  out.insert(out.end(), s.begin(), s.end());
}

}  // namespace

Bytes password_scalar(const Bytes& key_bytes) {
  const Bytes wide = crypto::hkdf_sha256(key_bytes, util::to_bytes("RPiLEDConfig"), util::to_bytes("spake2-w v2"), 40);
  if (wide.size() != 40) return {};

  Curve curve;
  if (!curve.ok()) return {};

  Big big(BN_bin2bn(wide.data(), static_cast<int>(wide.size()), nullptr));
  Big reduced(BN_new());
  if (!big || !reduced) return {};
  BN_set_flags(big.get(), BN_FLG_CONSTTIME);
  if (BN_nnmod(reduced.get(), big.get(), curve.order(), curve.ctx.get()) != 1) return {};

  return scalar_bytes(reduced.get());
}

Bytes random_scalar() {
  Curve curve;
  if (!curve.ok()) return {};

  // Rejection sampling, as RFC 9382 section 7 recommends: n is just under 2^256, so a
  // draw is refused about once in 2^32.
  for (int attempt = 0; attempt < 64; ++attempt) {
    const Bytes candidate = crypto::random_bytes(scalar_length);
    if (candidate.size() != scalar_length) return {};
    if (curve.scalar(candidate)) return candidate;
  }
  return {};
}

Bytes share(Role role, const Bytes& w, const Bytes& scalar) {
  Curve curve;
  if (!curve.ok()) return {};

  const Big w_big = curve.scalar(w);
  const Big s_big = curve.scalar(scalar);
  Point out = curve.make();
  if (!w_big || !s_big || !out) return {};

  // s*P + w*M (or N) in one call.
  const EC_POINT* blind = role == Role::a ? curve.m.get() : curve.n.get();
  if (EC_POINT_mul(curve.group.get(), out.get(), s_big.get(), blind, w_big.get(), curve.ctx.get()) != 1) return {};

  return curve.encode(out.get());
}

types::Result<Keys> finish(Role role, const Bytes& w, const Bytes& scalar, const Bytes& own_share, const Bytes& peer_share, const Bytes& a_identity,
                           const Bytes& b_identity, const Bytes& aad) {
  using R = types::Result<Keys>;

  Curve curve;
  if (!curve.ok()) return R::failure("OpenSSL cannot set up P-256");

  const Big w_big = curve.scalar(w);
  const Big s_big = curve.scalar(scalar);
  if (!w_big || !s_big) return R::failure("a scalar out of range");

  // Uncompressed only: the protocol says 65 bytes, and a compressed point is a malformed
  // share however OpenSSL would read it.
  if (peer_share.size() != share_length || peer_share[0] != 0x04) return R::failure("the peer's share is not a 65-byte uncompressed point");
  const Point peer = curve.decode(peer_share);
  if (!peer) return R::failure("the peer's share is not a point on P-256");

  // K = s * (peer - w*N) for A, s * (peer - w*M) for B.
  const EC_POINT* peer_blind = role == Role::a ? curve.n.get() : curve.m.get();
  Point unblind = curve.make();
  Point base = curve.make();
  Point k = curve.make();
  if (!unblind || !base || !k) return R::failure("OpenSSL cannot allocate a point");

  if (EC_POINT_mul(curve.group.get(), unblind.get(), nullptr, peer_blind, w_big.get(), curve.ctx.get()) != 1) return R::failure("w times the blind failed");
  if (EC_POINT_invert(curve.group.get(), unblind.get(), curve.ctx.get()) != 1) return R::failure("negation failed");
  if (EC_POINT_add(curve.group.get(), base.get(), peer.get(), unblind.get(), curve.ctx.get()) != 1) return R::failure("addition failed");
  if (EC_POINT_mul(curve.group.get(), k.get(), nullptr, base.get(), s_big.get(), curve.ctx.get()) != 1) return R::failure("the shared point failed");
  if (EC_POINT_is_at_infinity(curve.group.get(), k.get()) == 1) return R::failure("the shared point is the identity");

  Keys keys;
  keys.k = curve.encode(k.get());
  if (keys.k.empty()) return R::failure("cannot encode the shared point");

  const Bytes& p_a = role == Role::a ? own_share : peer_share;
  const Bytes& p_b = role == Role::a ? peer_share : own_share;

  append_with_length(keys.tt, a_identity);
  append_with_length(keys.tt, b_identity);
  append_with_length(keys.tt, p_a);
  append_with_length(keys.tt, p_b);
  append_with_length(keys.tt, keys.k);
  append_with_length(keys.tt, w);

  const Bytes hash = crypto::sha256(keys.tt);
  if (hash.size() != crypto::sha256_length) return R::failure("SHA-256 failed");
  keys.ke.assign(hash.begin(), hash.begin() + 16);
  const Bytes ka(hash.begin() + 16, hash.end());

  Bytes info = util::to_bytes("ConfirmationKeys");
  info.insert(info.end(), aad.begin(), aad.end());
  const Bytes kc = crypto::hkdf_sha256(ka, {}, info, 32);
  if (kc.size() != 32) return R::failure("HKDF failed");
  keys.kca.assign(kc.begin(), kc.begin() + 16);
  keys.kcb.assign(kc.begin() + 16, kc.end());

  keys.ca = crypto::hmac_sha256(keys.kca, keys.tt);
  keys.cb = crypto::hmac_sha256(keys.kcb, keys.tt);
  if (keys.ca.size() != confirmation_length || keys.cb.size() != confirmation_length) return R::failure("HMAC failed");

  return R::success(std::move(keys));
}

}  // namespace rpiledconfig::protocol::spake2
