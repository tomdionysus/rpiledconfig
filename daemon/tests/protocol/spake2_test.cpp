//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/spake2.h"

#include <gtest/gtest.h>

#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

types::Bytes hex(const std::string& text) { return util::from_hex(text).value(); }

struct RfcVector {
  std::string a, b, w, x, y, p_a, p_b, k, ke, kca, kcb, c_a, c_b;
};

// RFC 9382, Appendix B, P256-SHA256-HKDF-HMAC: the first vector (both identities) and the
// last (neither). No AAD.
const RfcVector rfc_vectors[] = {
    {"server", "client", "2ee57912099d31560b3a44b1184b9b4866e904c49d12ac5042c97dca461b1a5f", "43dd0fd7215bdcb482879fca3220c6a968e66d70b1356cac18bb26c84a78d729",
     "dcb60106f276b02606d8ef0a328c02e4b629f84f89786af5befb0bc75b6e66be",
     "04a56fa807caaa53a4d28dbb9853b9815c61a411118a6fe516a8798434751470f9010153ac33d0d5f2047ffdb1a3e42c9b4e6be662766e1eeb4116988ede5f912c",
     "0406557e482bd03097ad0cbaa5df82115460d951e3451962f1eaf4367a420676d09857ccbc522686c83d1852abfa8ed6e4a1155cf8f1543ceca528afb591a1e0b7",
     "0412af7e89717850671913e6b469ace67bd90a4df8ce45c2af19010175e37eed69f75897996d539356e2fa6a406d528501f907e04d97515fbe83db277b715d3325",
     "0e0672dc86f8e45565d338b0540abe69", "00c12546835755c86d8c0db7851ae86f", "a9fa3406c3b781b93d804485430ca27a",
     "58ad4aa88e0b60d5061eb6b5dd93e80d9c4f00d127c65b3b35b1b5281fee38f0", "d3e2e547f1ae04f2dbdbf0fc4b79f8ecff2dff314b5d32fe9fcef2fb26dc459b"},
    {"", "", "7bf46c454b4c1b25799527d896508afd5fc62ef4ec59db1efb49113063d70cca", "8cef65df64bb2d0f83540c53632de911b5b24b3eab6cc74a97609fd659e95473",
     "d7a66f64074a84652d8d623a92e20c9675c61cb5b4f6a0063e4648a2fdc02d53",
     "04a65b367a3f613cf9f0654b1b28a1e3a8a40387956c8ba6063e8658563890f46ca1ef6a676598889fc28de2950ab8120b79a5ef1ea4c9f44bc98f585634b46d66",
     "04589f13218822710d98d8b2123a079041052d9941b9cf88c6617ddb2fcc0494662eea8ba6b64692dc318250030c6af045cb738bc81ba35b043c3dcb46adf6f58d",
     "041a3c03d51b452537ca2a1fea6110353c6d5ed483c4f0f86f4492ca3f378d40a994b4477f93c64d928edbbcd3e85a7c709b7ea73ee97986ce3d1438e135543772",
     "fc6374762ba5cf11f4b2caa08b2cd1b9", "5dbd2f477166b7fb6d61febbd77a5563", "7689b4654407a5faeffdc8f18359d8a3",
     "dfb4db8d48ae5a675963ea5e6c19d98d4ea028d8e898dad96ea19a80ade95dca", "d0f0609d1613138d354f7e95f19fb556bf52d751947241e8c7118df5ef0ae175"},
};

}  // namespace

TEST(Spake2Test, ReproducesRfc9382sVectorsFromBothSides) {
  for (const auto& v : rfc_vectors) {
    SCOPED_TRACE("A='" + v.a + "', B='" + v.b + "'");
    const auto w = hex(v.w);
    const auto p_a = spake2::share(spake2::Role::a, w, hex(v.x));
    const auto p_b = spake2::share(spake2::Role::b, w, hex(v.y));
    EXPECT_EQ(util::to_hex(p_a), v.p_a);
    EXPECT_EQ(util::to_hex(p_b), v.p_b);

    const auto a = spake2::finish(spake2::Role::a, w, hex(v.x), p_a, p_b, util::to_bytes(v.a), util::to_bytes(v.b), {});
    const auto b = spake2::finish(spake2::Role::b, w, hex(v.y), p_b, p_a, util::to_bytes(v.a), util::to_bytes(v.b), {});
    ASSERT_TRUE(a) << a.error;
    ASSERT_TRUE(b) << b.error;

    for (const auto* keys : {&a.value, &b.value}) {
      EXPECT_EQ(util::to_hex(keys->k), v.k);
      EXPECT_EQ(util::to_hex(keys->ke), v.ke);
      EXPECT_EQ(util::to_hex(keys->kca), v.kca);
      EXPECT_EQ(util::to_hex(keys->kcb), v.kcb);
      EXPECT_EQ(util::to_hex(keys->ca), v.c_a);
      EXPECT_EQ(util::to_hex(keys->cb), v.c_b);
    }
  }
}

TEST(Spake2Test, AWrongPasswordGivesDifferentKeysOnEachSide) {
  const auto x = spake2::random_scalar();
  const auto y = spake2::random_scalar();
  const auto w = spake2::password_scalar(util::to_bytes("right"));
  const auto guess = spake2::password_scalar(util::to_bytes("wrong"));

  const auto p_a = spake2::share(spake2::Role::a, guess, x);
  const auto p_b = spake2::share(spake2::Role::b, w, y);
  const auto a = spake2::finish(spake2::Role::a, guess, x, p_a, p_b, {}, {}, {});
  const auto b = spake2::finish(spake2::Role::b, w, y, p_b, p_a, {}, {}, {});
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_NE(a.value.ke, b.value.ke);
  EXPECT_NE(a.value.ca, b.value.ca);
}

// RFC 9382, section 7: a share that is not a point of the group is refused, as is the
// identity, and a K at the identity.
TEST(Spake2Test, RefusesAShareThatIsNotAValidPoint) {
  const auto w = spake2::password_scalar(util::to_bytes("pw"));
  const auto y = spake2::random_scalar();
  const auto p_b = spake2::share(spake2::Role::b, w, y);
  auto p_a = spake2::share(spake2::Role::a, w, spake2::random_scalar());
  ASSERT_EQ(p_a.size(), spake2::share_length);

  auto off_curve = p_a;
  off_curve[64] ^= 0x01;
  EXPECT_FALSE(spake2::finish(spake2::Role::b, w, y, p_b, off_curve, {}, {}, {}));

  types::Bytes identity(spake2::share_length, 0x00);
  identity[0] = 0x04;
  EXPECT_FALSE(spake2::finish(spake2::Role::b, w, y, p_b, identity, {}, {}, {}));
  EXPECT_FALSE(spake2::finish(spake2::Role::b, w, y, p_b, types::Bytes{0x00}, {}, {}, {}));

  // The same point compressed: valid on the curve, but not the protocol's form.
  types::Bytes compressed(p_a.begin(), p_a.begin() + 33);
  compressed[0] = static_cast<std::uint8_t>(0x02 | (p_a[64] & 1));
  EXPECT_FALSE(spake2::finish(spake2::Role::b, w, y, p_b, compressed, {}, {}, {}));

  types::Bytes short_share(p_a.begin(), p_a.end() - 1);
  EXPECT_FALSE(spake2::finish(spake2::Role::b, w, y, p_b, short_share, {}, {}, {}));
}

TEST(Spake2Test, RandomScalarsAre32BytesAndDiffer) {
  const auto a = spake2::random_scalar();
  const auto b = spake2::random_scalar();
  EXPECT_EQ(a.size(), spake2::scalar_length);
  EXPECT_EQ(b.size(), spake2::scalar_length);
  EXPECT_NE(a, b);
}

TEST(Spake2Test, RefusesAZeroOrOutOfRangeScalar) {
  const auto w = spake2::password_scalar(util::to_bytes("pw"));
  EXPECT_TRUE(spake2::share(spake2::Role::a, w, types::Bytes(32, 0x00)).empty());
  EXPECT_TRUE(spake2::share(spake2::Role::a, w, types::Bytes(32, 0xFF)).empty());
  EXPECT_TRUE(spake2::share(spake2::Role::a, w, types::Bytes(31, 0x01)).empty());
}
