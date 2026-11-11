//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "util.h"

#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <string>
#include <vector>

namespace util = rpiledconfig::util;

TEST(UtilTest, HexIsLowercaseOutAndEitherCaseIn) {
  const std::vector<std::uint8_t> bytes{0x00, 0x0f, 0xa5, 0xff};
  EXPECT_EQ(util::to_hex(bytes), "000fa5ff");
  EXPECT_EQ(util::to_hex(bytes.data(), 0), "");
  EXPECT_EQ(util::from_hex("000fa5ff"), bytes);
  EXPECT_EQ(util::from_hex("000FA5FF"), bytes);
  EXPECT_EQ(util::from_hex(""), std::vector<std::uint8_t>{});
}

TEST(UtilTest, HexIsRefusedAtAnOddLengthOrAnyOtherCharacter) {
  EXPECT_FALSE(util::from_hex("abc").has_value());
  for (const std::string text : {"0g", "g0", "0G", "-1", " 1", "0x", "\xc3\xa9"}) {
    EXPECT_FALSE(util::from_hex(text).has_value()) << text;
  }
}

TEST(UtilTest, BytesAndTextConvertWithoutChange) {
  const std::string text("a\0b\xff", 4);
  EXPECT_EQ(util::to_bytes(text), (std::vector<std::uint8_t>{'a', 0, 'b', 0xff}));
  EXPECT_EQ(util::to_string(util::to_bytes(text)), text);
}

// What came over the air must not forge a log line or drive a terminal.
TEST(UtilTest, PrintableReplacesEveryControlCharacter) {
  EXPECT_EQ(util::printable("Home\n[INFO ] forged\r\x1b[2J\x7f\t"), "Home?[INFO ] forged??[2J??");
  EXPECT_EQ(util::printable("caf\xc3\xa9 ok"), "caf\xc3\xa9 ok");
}

TEST(UtilTest, ZuluTimeIsUtcToTheMillisecond) {
  const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  const std::string stamp = util::zulu_time();
  const auto after = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  // YYYY-MM-DDTHH:MM:SS.mmmZ: digits everywhere but the separators.
  const std::string shape = "dddd-dd-ddTdd:dd:dd.dddZ";
  ASSERT_EQ(stamp.size(), shape.size()) << stamp;
  for (std::size_t i = 0; i < shape.size(); ++i) {
    if (shape[i] == 'd') {
      EXPECT_TRUE(stamp[i] >= '0' && stamp[i] <= '9') << stamp;
    } else {
      EXPECT_EQ(stamp[i], shape[i]) << stamp;
    }
  }

  std::tm parsed{};
  ASSERT_NE(strptime(stamp.c_str(), "%Y-%m-%dT%H:%M:%S", &parsed), nullptr);
  const auto seconds = timegm(&parsed);
  EXPECT_GE(seconds, before);
  EXPECT_LE(seconds, after);
}
