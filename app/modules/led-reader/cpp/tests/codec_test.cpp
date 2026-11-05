//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "codec.h"

#include <gtest/gtest.h>

#include <string>

#include "synth.h"

using namespace rpiledconfig::reader;

namespace {

std::string hex(const Bytes& bytes) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (const auto b : bytes) {
    out += digits[b >> 4];
    out += digits[b & 15];
  }
  return out;
}

const Id vectors_id = test::id_from(0x0123456789abcdefULL);

}  // namespace

// docs/protocol.md, "Test vectors", computed outside this code base.
TEST(CodecTest, TheCrcCheckValue) {
  const std::string text = "123456789";
  EXPECT_EQ(crc16(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), 0x29B1);
}

TEST(CodecTest, TheVectorsMessageCodewordAndGenerator) {
  EXPECT_EQ(hex(message_for(vectors_id)), "0123456789abcdef008c");
  EXPECT_EQ(hex(codeword_for(vectors_id)), "0123456789abcdef008cc420bb0f15c9");
  EXPECT_EQ(hex(rs_generator(6)), "013f01da20e326");
}

TEST(CodecTest, TheVectorsFrameSymbols) {
  std::string symbols;
  for (const auto s : frame_symbols_for(vectors_id)) symbols += s ? '1' : '0';
  EXPECT_EQ(symbols,
            "111110000000100001010011001001010100101101100011011001010011101001010110110110011101011011001000010010010101101011001010001100010010101101010010011011"
            "00101010111011010011");
}

TEST(CodecTest, TheWordsAreTheSixteenWithoutARunOfThree) {
  int count = 0;
  for (int w = 0; w < 32; ++w) {
    bool run_of_three = false;
    for (int shift = 0; shift <= 2; ++shift) run_of_three = run_of_three || ((w >> shift) & 7) == 0 || ((w >> shift) & 7) == 7;
    if (run_of_three) continue;
    EXPECT_EQ(words[static_cast<std::size_t>(count)], w);
    ++count;
  }
  EXPECT_EQ(count, 16);
}

// docs/protocol.md, "Test vectors": 3 bytes changed, 6 erased, 2 changed and 2 erased.
TEST(CodecTest, RepairsWhatTheCodeReaches) {
  const Bytes good = codeword_for(vectors_id);

  Bytes changed = good;
  changed[0] ^= 0x55;
  changed[7] ^= 0x01;
  changed[15] ^= 0xFF;
  auto read = read_codeword(changed, {});
  ASSERT_TRUE(read);
  EXPECT_EQ(read->id, vectors_id);
  EXPECT_EQ(read->errors, 3);

  Bytes erased = good;
  for (const int p : {1, 3, 5, 8, 9, 14}) erased[static_cast<std::size_t>(p)] = 0;
  read = read_codeword(erased, {1, 3, 5, 8, 9, 14});
  ASSERT_TRUE(read);
  EXPECT_EQ(read->id, vectors_id);
  EXPECT_EQ(read->erasures, 6);

  Bytes mixed = good;
  mixed[2] ^= 0x10;
  mixed[11] ^= 0x80;
  mixed[4] = 0;
  mixed[12] = 0;
  read = read_codeword(mixed, {4, 12});
  ASSERT_TRUE(read);
  EXPECT_EQ(read->id, vectors_id);
  EXPECT_EQ(read->errors, 2);
  EXPECT_EQ(read->erasures, 2);
}

TEST(CodecTest, RefusesWhatTheCodeDoesNotReach) {
  Bytes bad = codeword_for(vectors_id);
  for (const int p : {0, 2, 4, 6}) bad[static_cast<std::size_t>(p)] ^= 0x5A;
  const auto read = read_codeword(bad, {});
  // Four wrong bytes: refused, or (rarely) miscorrected into a word the CRC refuses.
  EXPECT_FALSE(read);

  EXPECT_FALSE(read_codeword(codeword_for(vectors_id), {0, 1, 2, 3, 4, 5, 6})) << "seven erasures";
}

// The CRC covers the version byte: frame version 7's word with the CRC computed over another
// version is refused, though it is a valid Reed-Solomon codeword.
TEST(CodecTest, RejectsAWordWhoseCrcIsOverAnotherVersion) {
  Bytes checked{0x05};
  checked.insert(checked.end(), vectors_id.begin(), vectors_id.end());
  const auto crc = crc16(checked.data(), checked.size());
  Bytes message(vectors_id.begin(), vectors_id.end());
  message.push_back(static_cast<std::uint8_t>(crc >> 8));
  message.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  Bytes word = message;
  const Bytes parity = rs_parity(message, parity_length);
  word.insert(word.end(), parity.begin(), parity.end());
  ASSERT_TRUE(rs_decode(word, {}));
  EXPECT_FALSE(read_codeword(word, {}));
}
