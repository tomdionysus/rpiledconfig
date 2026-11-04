//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/fragments.h"

#include <gtest/gtest.h>

#include "protocol/vectors.h"
#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

types::Bytes counting(std::size_t length) {
  types::Bytes out;
  for (std::size_t i = 0; i < length; ++i) out.push_back(static_cast<std::uint8_t>(i & 0xFF));
  return out;
}

}  // namespace

TEST(FragmentsTest, AMessageThatFitsIsOneFragmentFlaggedLast) {
  const auto pieces = fragment(counting(19), 20);
  ASSERT_EQ(pieces.size(), 1u);
  EXPECT_EQ(pieces[0].size(), 20u);
  EXPECT_EQ(pieces[0][0], flag_last);
  EXPECT_EQ(types::Bytes(pieces[0].begin() + 1, pieces[0].end()), counting(19));
}

TEST(FragmentsTest, ALongerMessageIsCutWithOnlyTheLastFlagged) {
  const auto message = counting(39);
  const auto pieces = fragment(message, 20);
  ASSERT_EQ(pieces.size(), 3u);

  EXPECT_EQ(pieces[0][0], 0x00);
  EXPECT_EQ(pieces[0].size(), 20u);
  EXPECT_EQ(pieces[1][0], 0x00);
  EXPECT_EQ(pieces[1].size(), 20u);
  EXPECT_EQ(pieces[2][0], flag_last);
  EXPECT_EQ(pieces[2].size(), 2u);

  types::Bytes joined;
  for (const auto& piece : pieces) joined.insert(joined.end(), piece.begin() + 1, piece.end());
  EXPECT_EQ(joined, message);
}

// A message of exactly the payload size still ends on a flagged fragment, not an empty one
// after it.
TEST(FragmentsTest, AnExactFitNeedsNoEmptyTrailer) {
  const auto pieces = fragment(counting(38), 20);
  ASSERT_EQ(pieces.size(), 2u);
  EXPECT_EQ(pieces[1][0], flag_last);
  EXPECT_EQ(pieces[1].size(), 20u);
}

TEST(FragmentsTest, AnEmptyMessageIsOneFlagsByte) {
  const auto pieces = fragment({}, 20);
  ASSERT_EQ(pieces.size(), 1u);
  EXPECT_EQ(pieces[0], types::Bytes{flag_last});
}

TEST(FragmentsTest, TheVectorsStatusRequestAtTwentyBytes) {
  const auto v = vectors::compute();
  ASSERT_EQ(v.status_request_wire.size(), 39u) << "8 of counter, 15 of ciphertext, 16 of tag";

  const auto pieces = fragment(v.status_request_wire, 20);
  ASSERT_EQ(pieces.size(), 3u);
  EXPECT_EQ(pieces, v.status_request_fragments);
  EXPECT_EQ(util::to_hex(types::Bytes(pieces[0].begin(), pieces[0].begin() + 9)), "000000000000000000") << "flags 0, then counter 0";
  EXPECT_EQ(pieces[2].size(), 2u);
  EXPECT_EQ(pieces[2][0], flag_last);
}

TEST(FragmentsTest, ReassemblyGivesTheMessageBackOnTheLastFragment) {
  const auto message = counting(45);
  Reassembler reassembler;

  const auto pieces = fragment(message, 20);
  for (std::size_t i = 0; i + 1 < pieces.size(); ++i) {
    const auto pushed = reassembler.push(pieces[i]);
    ASSERT_TRUE(pushed) << pushed.error;
    EXPECT_FALSE(pushed.value);
  }

  const auto last = reassembler.push(pieces.back());
  ASSERT_TRUE(last) << last.error;
  ASSERT_TRUE(last.value);
  EXPECT_EQ(*last.value, message);
  EXPECT_EQ(reassembler.pending(), 0u);

  // And again: the reassembler is good for the next message.
  const auto again = reassembler.push(types::Bytes{flag_last, 0x42});
  ASSERT_TRUE(again && again.value);
  EXPECT_EQ(*again.value, types::Bytes{0x42});
}

TEST(FragmentsTest, UnknownFlagBitsAreRefused) {
  Reassembler reassembler;
  for (const std::uint8_t flags : {0x02, 0x80, 0x03, 0xFF}) {
    EXPECT_FALSE(reassembler.push(types::Bytes{flags, 0x01})) << "flags " << static_cast<int>(flags);
  }
  EXPECT_FALSE(reassembler.push({})) << "no flags byte at all";
}

TEST(FragmentsTest, AMessageLongerThan65536BytesIsRefused) {
  Reassembler reassembler;

  // 65536 exactly is fine.
  const auto pieces = fragment(counting(65536), 512);
  for (const auto& piece : pieces) ASSERT_TRUE(reassembler.push(piece));

  // One more byte is not.
  const auto too_long = fragment(counting(65537), 512);
  bool refused = false;
  for (const auto& piece : too_long) {
    if (!reassembler.push(piece)) {
      refused = true;
      break;
    }
  }
  EXPECT_TRUE(refused);
  EXPECT_EQ(reassembler.pending(), 0u) << "a refused message is dropped whole";
}
