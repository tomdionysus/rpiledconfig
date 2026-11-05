//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <memory>

namespace rpiledconfig::protocol::crypto {

namespace {

struct CipherContextDeleter {
  void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};

struct PkeyContextDeleter {
  void operator()(EVP_PKEY_CTX* ctx) const { EVP_PKEY_CTX_free(ctx); }
};

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;
using PkeyContext = std::unique_ptr<EVP_PKEY_CTX, PkeyContextDeleter>;

}  // namespace

Bytes sha256(const Bytes& data) {
  Bytes digest(EVP_MAX_MD_SIZE);
  unsigned int length = 0;

  if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1) return {};

  digest.resize(length);
  return digest;
}

Bytes hmac_sha256(const Bytes& key, const Bytes& data) {
  Bytes mac(EVP_MAX_MD_SIZE);
  unsigned int length = 0;

  if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data.data(), data.size(), mac.data(), &length) == nullptr) return {};

  mac.resize(length);
  return mac;
}

Bytes hkdf_sha256(const Bytes& ikm, const Bytes& salt, const Bytes& info, std::size_t length) {
  PkeyContext ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
  if (!ctx) return {};

  if (EVP_PKEY_derive_init(ctx.get()) != 1) return {};
  if (EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) != 1) return {};
  // RFC 5869: no salt is HashLen zeros, which is what OpenSSL uses when none is set; it
  // refuses a zero-length one outright, and SPAKE2 and S both derive with no salt.
  if (!salt.empty() && EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), salt.data(), static_cast<int>(salt.size())) != 1) return {};
  if (EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), ikm.data(), static_cast<int>(ikm.size())) != 1) return {};
  if (EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), info.data(), static_cast<int>(info.size())) != 1) return {};

  Bytes out(length);
  std::size_t out_length = length;
  if (EVP_PKEY_derive(ctx.get(), out.data(), &out_length) != 1 || out_length != length) return {};

  return out;
}

Bytes aes256gcm_seal(const Bytes& key, const Bytes& nonce, const Bytes& plaintext) {
  if (key.size() != 32 || nonce.size() != gcm_nonce_length) return {};

  CipherContext ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return {};

  if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) return {};
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1) return {};
  if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) return {};

  Bytes out(plaintext.size() + gcm_tag_length);
  int length = 0;
  int total = 0;

  if (!plaintext.empty()) {
    if (EVP_EncryptUpdate(ctx.get(), out.data(), &length, plaintext.data(), static_cast<int>(plaintext.size())) != 1) return {};
    total = length;
  }

  if (EVP_EncryptFinal_ex(ctx.get(), out.data() + total, &length) != 1) return {};
  total += length;

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, gcm_tag_length, out.data() + total) != 1) return {};

  out.resize(static_cast<std::size_t>(total) + gcm_tag_length);
  return out;
}

std::optional<Bytes> aes256gcm_open(const Bytes& key, const Bytes& nonce, const Bytes& ciphertext_and_tag) {
  if (key.size() != 32 || nonce.size() != gcm_nonce_length || ciphertext_and_tag.size() < gcm_tag_length) return std::nullopt;

  const std::size_t ciphertext_length = ciphertext_and_tag.size() - gcm_tag_length;

  CipherContext ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return std::nullopt;

  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) return std::nullopt;
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1) return std::nullopt;
  if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) return std::nullopt;

  Bytes out(ciphertext_length);
  int length = 0;
  int total = 0;

  if (ciphertext_length > 0) {
    if (EVP_DecryptUpdate(ctx.get(), out.data(), &length, ciphertext_and_tag.data(), static_cast<int>(ciphertext_length)) != 1) return std::nullopt;
    total = length;
  }

  // The tag is const to us, but the ctrl takes a void*.
  Bytes tag(ciphertext_and_tag.begin() + static_cast<std::ptrdiff_t>(ciphertext_length), ciphertext_and_tag.end());
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, gcm_tag_length, tag.data()) != 1) return std::nullopt;

  // Final is where the tag is checked; anything but 1 is a forgery or corruption.
  if (EVP_DecryptFinal_ex(ctx.get(), out.data() + total, &length) != 1) return std::nullopt;
  total += length;

  out.resize(static_cast<std::size_t>(total));
  return out;
}

Bytes random_bytes(std::size_t length) {
  Bytes out(length);
  if (length == 0) return out;
  if (RAND_bytes(out.data(), static_cast<int>(length)) != 1) return {};
  return out;
}

bool equal(const Bytes& a, const Bytes& b) {
  if (a.size() != b.size()) return false;
  if (a.empty()) return true;
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace rpiledconfig::protocol::crypto
