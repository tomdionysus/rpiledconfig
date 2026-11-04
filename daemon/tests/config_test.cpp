//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "config.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "mocks/logger_mock.h"

using rpiledconfig::Config;

namespace {

struct Loaded {
  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  Config config{logger};
  bool ok = false;

  explicit Loaded(const std::string& text, const std::filesystem::path& base = "/etc/rpiledconfig") { ok = config.load_from_string(text, base); }
};

}  // namespace

TEST(ConfigTest, TheExampleParses) {
  auto logger = std::make_shared<MockLogger>();
  Config config(logger);

  ASSERT_TRUE(config.load_from_file(std::string(RPILEDCONFIG_TEST_SOURCE_DIR) + "/config/rpiledconfig.example.conf")) << logger->text();

  EXPECT_EQ(config.log_level, rpiledconfig::loggers::LogLevel::info);
  EXPECT_EQ(config.state_dir, "/var/lib/rpiledconfig");
  EXPECT_EQ(config.led_driver, "sysfs");
  EXPECT_EQ(config.led_path, "/sys/class/leds/ACT");
  EXPECT_EQ(config.ble_driver, "bluez");
  EXPECT_EQ(config.ble_adapter, "hci0");
  EXPECT_EQ(config.wifi_driver, "networkmanager");
  EXPECT_EQ(config.wifi_interface, "wlan0");
  EXPECT_EQ(config.wifi_easyconnect, "wpa_supplicant");
  EXPECT_FALSE(config.setup_always);
  EXPECT_EQ(config.setup_timeout, 900u);
  EXPECT_EQ(config.setup_recheck, 60u);
}

// What the package installs as /etc/rpiledconfig/rpiledconfig.conf, generated for this
// build's prefix: the Pi's drivers and the state directory the unit makes.
TEST(ConfigTest, ThePackagedConfigurationParses) {
  auto logger = std::make_shared<MockLogger>();
  Config config(logger);

  ASSERT_TRUE(config.load_from_file(RPILEDCONFIG_TEST_PACKAGED_CONFIG)) << logger->text();

  EXPECT_EQ(config.state_dir, "/var/lib/rpiledconfig");
  EXPECT_EQ(config.led_driver, "sysfs");
  EXPECT_EQ(config.led_path, "/sys/class/leds/ACT");
  EXPECT_EQ(config.ble_driver, "bluez");
  EXPECT_EQ(config.wifi_driver, "networkmanager");
  EXPECT_EQ(config.setup_timeout, 900u);
}

TEST(ConfigTest, AnEmptyFileIsTheDefaults) {
  Loaded loaded("");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.log_level, rpiledconfig::loggers::LogLevel::info);
  EXPECT_EQ(loaded.config.state_dir, "/var/lib/rpiledconfig");
  EXPECT_EQ(loaded.config.led_path, "/sys/class/leds/ACT");
  EXPECT_EQ(loaded.config.ble_adapter, "hci0");
  EXPECT_FALSE(loaded.config.setup_always);
  EXPECT_EQ(loaded.config.setup_timeout, 900u);
  EXPECT_EQ(loaded.config.setup_recheck, 60u);

  // The real drivers where they exist, the fakes elsewhere.
#if defined(__linux__)
  EXPECT_EQ(loaded.config.led_driver, "sysfs");
  EXPECT_EQ(loaded.config.ble_driver, "bluez");
  EXPECT_EQ(loaded.config.wifi_driver, "networkmanager");
#else
  EXPECT_EQ(loaded.config.led_driver, "fake");
  EXPECT_EQ(loaded.config.ble_driver, "fake");
  EXPECT_EQ(loaded.config.wifi_driver, "fake");
#endif
}

TEST(ConfigTest, CommentsBlankLinesAndWhitespaceAreForgiven) {
  Loaded loaded(
      "# a comment\n"
      "\n"
      "   log.level :   debug   # trailing comment\n"
      "\t\n"
      "setup.always: true\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.log_level, rpiledconfig::loggers::LogLevel::debug);
  EXPECT_TRUE(loaded.config.setup_always);
}

TEST(ConfigTest, ValuesMayBeQuoted) {
  Loaded loaded(
      "ble.adapter: \"hci1\"\n"
      "led.path: '/sys/class/leds/led0'\n"
      "state.dir: \"/var/lib/with # hash\"\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.ble_adapter, "hci1");
  EXPECT_EQ(loaded.config.led_path, "/sys/class/leds/led0");
  EXPECT_EQ(loaded.config.state_dir, "/var/lib/with # hash");
}

TEST(ConfigTest, TheLedMayBeActiveLow) {
  EXPECT_FALSE(Loaded("").config.led_active_low);

  Loaded loaded("led.active_low: true\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();
  EXPECT_TRUE(loaded.config.led_active_low);
}

TEST(ConfigTest, AnUnclosedQuoteIsAnError) {
  Loaded loaded("ble.adapter: \"hci1\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.logger->contains("line 1"));
}

TEST(ConfigTest, AnUnknownKeyIsAnErrorNamingTheLine) {
  Loaded loaded(
      "log.level: info\n"
      "led.pth: /sys/class/leds/ACT\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.logger->contains("line 2")) << loaded.logger->text();
  EXPECT_TRUE(loaded.logger->contains("led.pth")) << loaded.logger->text();
}

TEST(ConfigTest, ALineThatIsNotKeyColonValueIsAnError) {
  Loaded loaded("just some words\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.logger->contains("line 1"));
}

TEST(ConfigTest, AKeySetTwiceIsAnError) {
  Loaded loaded(
      "setup.timeout: 10\n"
      "setup.timeout: 20\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.logger->contains("line 2"));
}

TEST(ConfigTest, RelativePathsResolveAgainstTheFile) {
  Loaded loaded(
      "state.dir: .local\n"
      "led.path: ../fake-led\n",
      "/srv/rpiledconfig/etc");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.state_dir, "/srv/rpiledconfig/etc/.local");
  EXPECT_EQ(loaded.config.led_path, "/srv/rpiledconfig/fake-led");
}

TEST(ConfigTest, BooleansAreTrueOrFalseAndNothingElse) {
  EXPECT_TRUE(Loaded("setup.always: false\n").ok);
  EXPECT_FALSE(Loaded("setup.always: yes\n").ok);
  EXPECT_FALSE(Loaded("setup.always: 1\n").ok);
}

TEST(ConfigTest, NumbersAreWholeAndPositive) {
  Loaded loaded(
      "setup.timeout: 300\n"
      "setup.recheck: 5\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();
  EXPECT_EQ(loaded.config.setup_timeout, 300u);
  EXPECT_EQ(loaded.config.setup_recheck, 5u);

  EXPECT_FALSE(Loaded("setup.timeout: ten\n").ok);
  EXPECT_FALSE(Loaded("setup.timeout: -1\n").ok);
  EXPECT_FALSE(Loaded("setup.timeout: 0\n").ok);
  EXPECT_FALSE(Loaded("setup.recheck: 0\n").ok);
}

TEST(ConfigTest, TheDriversAreNamedFromAFixedList) {
  EXPECT_TRUE(Loaded("led.driver: fake\nble.driver: fake\nwifi.driver: fake\n").ok);
  EXPECT_TRUE(Loaded("led.driver: sysfs\nble.driver: bluez\nwifi.driver: networkmanager\n").ok);

  EXPECT_FALSE(Loaded("led.driver: gpio\n").ok);
  EXPECT_FALSE(Loaded("ble.driver: hci\n").ok);
  EXPECT_FALSE(Loaded("wifi.driver: wpa\n").ok);
}

TEST(ConfigTest, EasyConnectFollowsTheWifiDriverUnlessSet) {
  EXPECT_EQ(Loaded("wifi.driver: fake\n").config.wifi_easyconnect, "fake") << "a fake WiFi never touches the radio";
  EXPECT_EQ(Loaded("wifi.driver: networkmanager\n").config.wifi_easyconnect, "wpa_supplicant");
  EXPECT_EQ(Loaded("wifi.driver: networkmanager\nwifi.easyconnect: off\n").config.wifi_easyconnect, "off");
  EXPECT_EQ(Loaded("wifi.driver: fake\nwifi.easyconnect: wpa_supplicant\n").config.wifi_easyconnect, "wpa_supplicant");
  EXPECT_FALSE(Loaded("wifi.easyconnect: dpp\n").ok);
}

TEST(ConfigTest, TheWifiInterfaceIsANameDefaultingToWlan0) {
  EXPECT_EQ(Loaded("").config.wifi_interface, "wlan0");
  EXPECT_EQ(Loaded("wifi.interface: wlan1\n").config.wifi_interface, "wlan1");
  EXPECT_FALSE(Loaded("wifi.interface: ../etc\n").ok);
}

TEST(ConfigTest, TheLogLevelIsOneOfFour) {
  EXPECT_EQ(Loaded("log.level: warn\n").config.log_level, rpiledconfig::loggers::LogLevel::warn);
  EXPECT_FALSE(Loaded("log.level: warning\n").ok);
}

TEST(ConfigTest, AMissingFileIsAnError) {
  auto logger = std::make_shared<MockLogger>();
  Config config(logger);
  EXPECT_FALSE(config.load_from_file("/nonexistent/rpiledconfig.conf"));
  EXPECT_TRUE(logger->contains("/nonexistent/rpiledconfig.conf"));
}

TEST(ConfigTest, LocateTakesTheCommandLineThenTheEnvironmentThenTheSystemPath) {
  unsetenv(Config::environment_variable);
  EXPECT_EQ(Config::locate("/from/cli.conf"), std::filesystem::path("/from/cli.conf"));
  EXPECT_EQ(Config::locate(""), std::filesystem::path(Config::system_path));

  setenv(Config::environment_variable, "/from/env.conf", 1);
  EXPECT_EQ(Config::locate(""), std::filesystem::path("/from/env.conf"));
  EXPECT_EQ(Config::locate("/from/cli.conf"), std::filesystem::path("/from/cli.conf"));
  unsetenv(Config::environment_variable);
}
