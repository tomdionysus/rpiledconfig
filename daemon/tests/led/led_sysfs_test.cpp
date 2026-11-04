//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led/led_sysfs.h"

#include <gtest/gtest.h>

#include <memory>

#include "helpers/temp_dir_helper.h"
#include "mocks/logger_mock.h"

using namespace rpiledconfig;

namespace {

// A directory pretending to be /sys/class/leds/ACT as the kernel presents it.
struct FakeNode {
  TempDir dir{"led"};

  FakeNode() {
    dir.write("trigger", "none timer heartbeat [mmc0] default-on\n");
    dir.write("brightness", "0\n");
    dir.write("max_brightness", "255\n");
  }
};

}  // namespace

TEST(LedSysfsTest, TheCurrentTriggerIsTheOneInBrackets) {
  EXPECT_EQ(led::parse_current_trigger("none timer [mmc0] heartbeat\n"), "mmc0");
  EXPECT_EQ(led::parse_current_trigger("[none] timer heartbeat"), "none");
  EXPECT_EQ(led::parse_current_trigger("none timer heartbeat"), "");
}

TEST(LedSysfsTest, BeginTakesTheTriggerOverAndEndGivesItBack) {
  FakeNode node;
  auto logger = std::make_shared<MockLogger>();
  led::LedSysfs led(logger, node.dir.path());

  const auto begun = led.begin();
  ASSERT_TRUE(begun) << begun.error;
  EXPECT_EQ(led.saved_trigger(), "mmc0");
  EXPECT_EQ(node.dir.read("trigger"), "none");

  led.set(true);
  EXPECT_EQ(node.dir.read("brightness"), "255") << "on is max_brightness";
  led.set(false);
  EXPECT_EQ(node.dir.read("brightness"), "0");

  led.end();
  EXPECT_EQ(node.dir.read("trigger"), "mmc0");
  EXPECT_EQ(node.dir.read("brightness"), "0");
}

// led.active_low: an LED that lights at brightness 0 (the Raspberry Pi 5's ACT) gets the
// opposite level written, so what the camera sees is what the protocol says.
TEST(LedSysfsTest, AnActiveLowLedIsDrivenInverted) {
  FakeNode node;
  auto logger = std::make_shared<MockLogger>();
  led::LedSysfs led(logger, node.dir.path(), true);
  ASSERT_TRUE(led.begin());

  led.set(true);
  EXPECT_EQ(node.dir.read("brightness"), "0") << "on is brightness 0 for an active-low LED";
  led.set(false);
  EXPECT_EQ(node.dir.read("brightness"), "255");
}

TEST(LedSysfsTest, ANodeThatIsNotThereIsRefused) {
  auto logger = std::make_shared<MockLogger>();
  led::LedSysfs led(logger, "/nonexistent/leds/ACT");

  const auto begun = led.begin();
  EXPECT_FALSE(begun);
  EXPECT_NE(begun.error.find("trigger"), std::string::npos);

  // end() without a begin() touches nothing and does not complain.
  led.end();
}

TEST(LedSysfsTest, AMissingMaxBrightnessMeansOne) {
  FakeNode node;
  std::filesystem::remove(node.dir.path() / "max_brightness");

  auto logger = std::make_shared<MockLogger>();
  led::LedSysfs led(logger, node.dir.path());
  ASSERT_TRUE(led.begin());

  led.set(true);
  EXPECT_EQ(node.dir.read("brightness"), "1");
}
