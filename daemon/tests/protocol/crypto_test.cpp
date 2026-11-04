//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/crypto.h"

#include <gtest/gtest.h>

#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

types::Bytes hex(const std::string& text) {
  const auto bytes = util::from_hex(text);
  return bytes ? *bytes : types::Bytes{};
}

}  // namespace

TEST(CryptoTest, Sha256OfAbc) {
  EXPECT_EQ(util::to_hex(crypto::sha256(util::to_bytes("abc"))), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(util::to_hex(crypto::sha256({})), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// RFC 4231, test case 2.
TEST(CryptoTest, HmacSha256Rfc4231TestCase2) {
  const auto mac = crypto::hmac_sha256(util::to_bytes("Jefe"), util::to_bytes("what do ya want for nothing?"));
  EXPECT_EQ(util::to_hex(mac), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// RFC 5869, test case 1.
TEST(CryptoTest, HkdfSha256Rfc5869TestCase1) {
  const auto ikm = hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
  const auto salt = hex("000102030405060708090a0b0c");
  const auto info = hex("f0f1f2f3f4f5f6f7f8f9");

  const auto okm = crypto::hkdf_sha256(ikm, salt, info, 42);
  EXPECT_EQ(util::to_hex(okm), "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
}

// NIST's GCM test case 14: AES-256, an all-zero key, nonce and block.
TEST(CryptoTest, Aes256GcmKnownAnswer) {
  const types::Bytes key(32, 0x00);
  const types::Bytes nonce(12, 0x00);
  const types::Bytes plaintext(16, 0x00);

  const auto sealed = crypto::aes256gcm_seal(key, nonce, plaintext);
  ASSERT_EQ(sealed.size(), 32u);
  EXPECT_EQ(util::to_hex(sealed), "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919");

  // And test case 13, nothing at all: just the tag.
  const auto empty = crypto::aes256gcm_seal(key, nonce, {});
  EXPECT_EQ(util::to_hex(empty), "530f8afbc74536b9a963b4f1c4cb738b");
}

TEST(CryptoTest, Aes256GcmRoundTripsAndDetectsTampering) {
  const auto key = crypto::random_bytes(32);
  const auto nonce = crypto::random_bytes(12);
  const auto plaintext = util::to_bytes("{\"op\":\"status\"}");

  auto sealed = crypto::aes256gcm_seal(key, nonce, plaintext);
  ASSERT_EQ(sealed.size(), plaintext.size() + crypto::gcm_tag_length);

  const auto opened = crypto::aes256gcm_open(key, nonce, sealed);
  ASSERT_TRUE(opened);
  EXPECT_EQ(*opened, plaintext);

  // One bit of ciphertext.
  auto tampered = sealed;
  tampered[0] ^= 0x01;
  EXPECT_FALSE(crypto::aes256gcm_open(key, nonce, tampered));

  // One bit of tag.
  tampered = sealed;
  tampered.back() ^= 0x80;
  EXPECT_FALSE(crypto::aes256gcm_open(key, nonce, tampered));

  // The wrong nonce, and the wrong key.
  auto other_nonce = nonce;
  other_nonce[11] ^= 0x01;
  EXPECT_FALSE(crypto::aes256gcm_open(key, other_nonce, sealed));
  EXPECT_FALSE(crypto::aes256gcm_open(crypto::random_bytes(32), nonce, sealed));

  // Too short to hold a tag.
  EXPECT_FALSE(crypto::aes256gcm_open(key, nonce, types::Bytes(15, 0x00)));
}

TEST(CryptoTest, TheWrongKeyOrNonceLengthIsRefusedNotPadded) {
  EXPECT_TRUE(crypto::aes256gcm_seal(types::Bytes(16, 0x00), types::Bytes(12, 0x00), {}).empty());
  EXPECT_TRUE(crypto::aes256gcm_seal(types::Bytes(32, 0x00), types::Bytes(16, 0x00), {}).empty());
}

TEST(CryptoTest, RandomBytesAreTheLengthAskedForAndDiffer) {
  const auto a = crypto::random_bytes(16);
  const auto b = crypto::random_bytes(16);
  EXPECT_EQ(a.size(), 16u);
  EXPECT_EQ(b.size(), 16u);
  EXPECT_NE(a, b);
  EXPECT_TRUE(crypto::random_bytes(0).empty());
}

TEST(CryptoTest, EqualComparesWholeValues) {
  EXPECT_TRUE(crypto::equal(hex("0102"), hex("0102")));
  EXPECT_FALSE(crypto::equal(hex("0102"), hex("0103")));
  EXPECT_FALSE(crypto::equal(hex("0102"), hex("010203")));
  EXPECT_TRUE(crypto::equal({}, {}));
}
