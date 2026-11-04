//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "machine_id/machine_id.h"

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <string>

#include "helpers/temp_dir_helper.h"

using namespace rpiledconfig::machine_id;

TEST(MachineIdTest, TheByteFormIsEightBytesBigEndian) {
  const auto id = MachineId::from_value(0x0123456789abcdefULL);

  const rpiledconfig::types::Bytes expected{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  EXPECT_EQ(id.id_bytes(), expected);
  EXPECT_EQ(id.value(), 0x0123456789abcdefULL);
  EXPECT_EQ(id.to_hex(), "0123456789abcdef");
  EXPECT_EQ(MachineId::from_value(0x45ULL).to_hex(), "0000000000000045");
}

TEST(MachineIdTest, HexRoundTripsAndForgivesTheFilesNewline) {
  const auto id = MachineId::from_hex("0123456789abcdef\n");
  ASSERT_TRUE(id);
  EXPECT_EQ(id->value(), 0x0123456789abcdefULL);

  EXPECT_TRUE(MachineId::from_hex("0123456789ABCDEF"));
  EXPECT_FALSE(MachineId::from_hex("0123456789abcde"));
  EXPECT_FALSE(MachineId::from_hex("0123456789abcdef00"));
  EXPECT_FALSE(MachineId::from_hex("0123456789abcdeg"));
  EXPECT_FALSE(MachineId::from_hex(""));
}

// docs/protocol.md, "The machine ID": an id from frame version 4 (6 hex characters) or 3
// (12) is an error rather than something to pad, and is never overwritten unasked.
TEST(MachineIdTest, AnIdFromFrameVersionFourOrThreeIsRefused) {
  for (const std::string old : {"012345\n", "0123456789ab\n"}) {
    EXPECT_FALSE(MachineId::from_hex(old)) << old;

    TempDir dir("machine-id");
    dir.write("machine-id", old);
    const auto loaded = load(dir.path());
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error.find("16 hex characters"), std::string::npos) << loaded.error;
    EXPECT_FALSE(load_or_generate(dir.path())) << "not overwritten unasked";
    EXPECT_EQ(dir.read("machine-id"), old);
  }
}

TEST(MachineIdTest, TheFirstStartMakesTheFileAndALaterStartReadsItBack) {
  TempDir dir("machine-id");
  const auto state = dir.path() / "state";

  const auto first = load_or_generate(state);
  ASSERT_TRUE(first) << first.error;

  // 16 lowercase hex characters and a newline, mode 0600.
  const auto text = dir.read("state/machine-id");
  ASSERT_EQ(text.size(), 17u);
  EXPECT_EQ(text, first.value.to_hex() + "\n");
  for (const char c : text.substr(0, 16)) EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << c;

  struct stat st{};
  ASSERT_EQ(::stat(path_in(state).c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);

  const auto second = load_or_generate(state);
  ASSERT_TRUE(second) << second.error;
  EXPECT_EQ(second.value, first.value);

  const auto loaded = load(state);
  ASSERT_TRUE(loaded) << loaded.error;
  EXPECT_EQ(loaded.value, first.value);
}

TEST(MachineIdTest, RegenerateReplacesTheIdForGood) {
  TempDir dir("machine-id");

  const auto first = generate(dir.path());
  ASSERT_TRUE(first) << first.error;

  const auto second = generate(dir.path());
  ASSERT_TRUE(second) << second.error;
  EXPECT_NE(second.value, first.value);

  const auto loaded = load(dir.path());
  ASSERT_TRUE(loaded) << loaded.error;
  EXPECT_EQ(loaded.value, second.value);
}

TEST(MachineIdTest, AFileThatIsNotAnIdIsRefused) {
  TempDir dir("machine-id");
  dir.write("machine-id", "not an id\n");

  const auto loaded = load(dir.path());
  EXPECT_FALSE(loaded);
  EXPECT_NE(loaded.error.find("machine-id"), std::string::npos);

  EXPECT_FALSE(load(dir.path() / "nowhere"));
}

TEST(MachineIdTest, AnEmptyFileCountsAsNoIdAndIsReplaced) {
  TempDir dir("machine-id");
  dir.write("machine-id", "");

  // --show-id reads without generating: an empty file is still no ID to show.
  EXPECT_FALSE(load(dir.path()));

  const auto generated = load_or_generate(dir.path());
  ASSERT_TRUE(generated) << generated.error;
  EXPECT_EQ(dir.read("machine-id"), generated.value.to_hex() + "\n");

  struct stat st{};
  ASSERT_EQ(::stat(path_in(dir.path()).c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
}

TEST(MachineIdTest, AFileThatIsNotAnIdIsNotOverwritten) {
  TempDir dir("machine-id");
  dir.write("machine-id", "not an id\n");

  EXPECT_FALSE(load_or_generate(dir.path()));
  EXPECT_EQ(dir.read("machine-id"), "not an id\n");
}

TEST(MachineIdTest, GenerateLeavesNoTemporaryFileBehind) {
  TempDir dir("machine-id");

  ASSERT_TRUE(generate(dir.path()));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "machine-id.tmp"));
}

TEST(MachineIdTest, TwoDrawsDiffer) {
  const auto a = random_id();
  const auto b = random_id();
  ASSERT_TRUE(a) << a.error;
  ASSERT_TRUE(b) << b.error;
  EXPECT_NE(a.value, b.value);
}
