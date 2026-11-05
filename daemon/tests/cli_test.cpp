//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "cli.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

rpiledconfig::cli::Options parse(std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), "rpiledconfig");

  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());

  return rpiledconfig::cli::parse(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST(CliTest, NothingIsFine) {
  const auto options = parse({});
  EXPECT_TRUE(options.ok);
  EXPECT_TRUE(options.config.empty());
  EXPECT_FALSE(options.print_vectors);
}

TEST(CliTest, TheConfigTakesEitherSpelling) {
  EXPECT_EQ(parse({"--config", "/a.conf"}).config, "/a.conf");
  EXPECT_EQ(parse({"--config=/b.conf"}).config, "/b.conf");
  EXPECT_EQ(parse({"-c", "/c.conf"}).config, "/c.conf");
}

TEST(CliTest, AMissingValueIsAnError) {
  EXPECT_FALSE(parse({"--config"}).ok);
  EXPECT_FALSE(parse({"--config="}).ok);
  EXPECT_FALSE(parse({"-c", ""}).ok);
}

TEST(CliTest, AnUnknownOptionStopsTheDaemonRatherThanBeingIgnored) {
  const auto options = parse({"--confg", "/a.conf"});
  EXPECT_FALSE(options.ok);
  EXPECT_NE(options.error.find("--confg"), std::string::npos);
}

TEST(CliTest, TheVectorQuestion) {
  EXPECT_TRUE(parse({"--print-vectors"}).print_vectors);

  const auto both = parse({"--print-vectors", "--config", "/a.conf"});
  EXPECT_TRUE(both.ok);
  EXPECT_TRUE(both.print_vectors);
  EXPECT_EQ(both.config, "/a.conf");
}

// docs/protocol.md, The pairing key: no command prints the key, and none makes one.
TEST(CliTest, ThereIsNoWayToAskForTheKey) {
  EXPECT_FALSE(parse({"--show-id"}).ok);
  EXPECT_FALSE(parse({"--regenerate-id"}).ok);
}

TEST(CliTest, VersionAndHelp) {
  EXPECT_TRUE(parse({"--version"}).version);
  EXPECT_TRUE(parse({"-v"}).version);
  EXPECT_TRUE(parse({"--help"}).help);
  EXPECT_TRUE(parse({"-h"}).help);

  const auto usage = rpiledconfig::cli::usage();
  EXPECT_NE(usage.find("RPILEDCONFIG_CONFIG"), std::string::npos);
  EXPECT_NE(usage.find("/etc/rpiledconfig/rpiledconfig.conf"), std::string::npos);
  EXPECT_NE(usage.find("--print-vectors"), std::string::npos);
}
