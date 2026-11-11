//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
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
  EXPECT_EQ(config.led_driver, "sysfs");
  EXPECT_EQ(config.led_path, "/sys/class/leds/ACT");
  EXPECT_EQ(config.ble_driver, "bluez");
  EXPECT_EQ(config.ble_adapter, "hci0");
  EXPECT_EQ(config.wifi_driver, "networkmanager");
  EXPECT_EQ(config.wifi_interface, "wlan0");
  EXPECT_EQ(config.wifi_easyconnect, "wpa_supplicant");
  EXPECT_TRUE(config.setup_always);
  EXPECT_EQ(config.setup_timeout, 900u);
  EXPECT_EQ(config.setup_recheck, 60u);
}

// What the package installs as /etc/rpiledconfig/rpiledconfig.conf, generated for this
// build's prefix: the Pi's drivers and the state directory the unit makes.
TEST(ConfigTest, ThePackagedConfigurationParses) {
  auto logger = std::make_shared<MockLogger>();
  Config config(logger);

  ASSERT_TRUE(config.load_from_file(RPILEDCONFIG_TEST_PACKAGED_CONFIG)) << logger->text();

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
  EXPECT_EQ(loaded.config.led_path, "/sys/class/leds/ACT");
  EXPECT_EQ(loaded.config.ble_adapter, "hci0");
  EXPECT_TRUE(loaded.config.setup_always) << "a Pi with the package installed is meant to be reachable";
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
      "setup.always: false\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.log_level, rpiledconfig::loggers::LogLevel::debug);
  EXPECT_FALSE(loaded.config.setup_always);
}

TEST(ConfigTest, ValuesMayBeQuoted) {
  Loaded loaded(
      "ble.adapter: \"hci1\"\n"
      "led.path: \"/sys/class/leds/with # hash\"\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.ble_adapter, "hci1");
  EXPECT_EQ(loaded.config.led_path, "/sys/class/leds/with # hash");
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

// Double quotes take \n, \t, \\ and \"; single quotes take everything as it is.
TEST(ConfigTest, QuotedValuesTakeTheirEscapes) {
  Loaded escaped("ble.name_prefix: \"a\\\"b\\\\c\" # comment\n");
  ASSERT_TRUE(escaped.ok) << escaped.logger->text();
  EXPECT_EQ(escaped.config.ble_name_prefix, "a\"b\\c");

  Loaded single("led.path: '/sys/class/leds/a\\nb'\n");
  ASSERT_TRUE(single.ok) << single.logger->text();
  EXPECT_EQ(single.config.led_path, "/sys/class/leds/a\\nb");

  // A newline or a tab is taken; the name prefix then refuses it as not printable.
  Loaded control("ble.name_prefix: \"a\\tb\"\n");
  EXPECT_FALSE(control.ok);
  Loaded newline("led.path: \"/a\\nb\"\n");
  ASSERT_TRUE(newline.ok) << newline.logger->text();
  EXPECT_EQ(newline.config.led_path, "/a\nb");
}

TEST(ConfigTest, AnUnknownEscapeOrTextAfterTheQuoteIsAnError) {
  Loaded unknown("ble.adapter: \"hci\\x1\"\n");
  EXPECT_FALSE(unknown.ok);
  EXPECT_TRUE(unknown.logger->contains("unknown escape \\x")) << unknown.logger->text();

  Loaded after("ble.adapter: \"hci1\" hci2\n");
  EXPECT_FALSE(after.ok);
  EXPECT_TRUE(after.logger->contains("text after the closing quote: hci2")) << after.logger->text();
}

TEST(ConfigTest, AKeyWithASpaceInItIsAnError) {
  Loaded loaded("setup always: true\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.logger->contains("no key before the colon")) << loaded.logger->text();
  EXPECT_FALSE(Loaded(": true\n").ok);
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
  Loaded loaded("led.path: ../fake-led\n", "/srv/rpiledconfig/etc");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();

  EXPECT_EQ(loaded.config.led_path, "/srv/rpiledconfig/fake-led");
}

// A conffile an upgrade left in place still has this line, and the daemon must still start.
TEST(ConfigTest, TheOldStateDirIsTakenAndWarnedAbout) {
  Loaded loaded("state.dir: /var/lib/rpiledconfig\n");
  ASSERT_TRUE(loaded.ok) << loaded.logger->text();
  EXPECT_TRUE(loaded.logger->contains("state.dir is no longer used")) << loaded.logger->text();
}

// docs/protocol.md, BLE: what fits in the scan response.
TEST(ConfigTest, TheNamePrefixIsChecked) {
  EXPECT_EQ(Loaded("").config.ble_name_prefix, "RPiLEDConfig");
  EXPECT_EQ(Loaded("ble.name_prefix: Kitchen Pi\n").config.ble_name_prefix, "Kitchen Pi");

  const std::string longest(24, 'x');
  Loaded fits("ble.name_prefix: " + longest + "\n");
  EXPECT_TRUE(fits.ok) << fits.logger->text();
  EXPECT_EQ(fits.config.ble_name_prefix, longest);

  EXPECT_FALSE(Loaded("ble.name_prefix: " + longest + "x\n").ok);
  EXPECT_FALSE(Loaded("ble.name_prefix: caf\xc3\xa9\n").ok);
  EXPECT_FALSE(Loaded("ble.name_prefix:\n").ok);
}

// The phone chooses the rate now (docs/protocol.md, Symbols).
TEST(ConfigTest, TheSymbolTimeIsNoLongerAKey) { EXPECT_FALSE(Loaded("led.symbol_ms: 50\n").ok); }

TEST(ConfigTest, TheAdapterKeepsItsNameUnlessSetOtherwise) {
  EXPECT_FALSE(Loaded("").config.ble_hide_adapter_name);
  EXPECT_TRUE(Loaded("ble.hide_adapter_name: true\n").config.ble_hide_adapter_name);
  EXPECT_FALSE(Loaded("ble.hide_adapter_name: yes\n").ok);
}

TEST(ConfigTest, TheServiceRunsContinuouslyUnlessToldToStopAfterBoot) {
  EXPECT_EQ(Loaded("").config.service_stop_after_boot_minutes, 0u);
  EXPECT_EQ(Loaded("service.stop_after_boot_minutes: 30\n").config.service_stop_after_boot_minutes, 30u);
  EXPECT_FALSE(Loaded("service.stop_after_boot_minutes: soon\n").ok);

  using std::chrono::seconds;
  EXPECT_FALSE(rpiledconfig::time_left_after_boot(0, seconds(0)).has_value()) << "0 is no limit";
  EXPECT_FALSE(rpiledconfig::time_left_after_boot(0, seconds(999999)).has_value());
  EXPECT_EQ(rpiledconfig::time_left_after_boot(30, seconds(0)), seconds(1800));
  EXPECT_EQ(rpiledconfig::time_left_after_boot(30, seconds(1799)), seconds(1));
  EXPECT_EQ(rpiledconfig::time_left_after_boot(30, seconds(1800)), seconds(0));
  EXPECT_EQ(rpiledconfig::time_left_after_boot(30, seconds(99999)), seconds(0));
}

TEST(ConfigTest, TheHostnameIsReportedUnlessSetOtherwise) {
  EXPECT_TRUE(Loaded("").config.status_report_hostname);
  EXPECT_FALSE(Loaded("status.report_hostname: false\n").config.status_report_hostname);
  EXPECT_FALSE(Loaded("status.report_hostname: no\n").ok);
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

TEST(ConfigTest, TheUninstallDriverIsSystemdFakeOrOff) {
  EXPECT_EQ(Loaded("").config.uninstall_driver, rpiledconfig::Config::default_uninstall_driver);
  for (const std::string driver : {"systemd", "fake", "off"}) {
    EXPECT_EQ(Loaded("uninstall.driver: " + driver + "\n").config.uninstall_driver, driver);
  }
  EXPECT_FALSE(Loaded("uninstall.driver: apt\n").ok);
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
