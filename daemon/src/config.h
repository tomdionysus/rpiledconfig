//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "loggers/logger.h"

namespace rpiledconfig {

// The configuration, parsed once at startup into flat fields. Every key is documented in
// config/rpiledconfig.example.conf, and that file and this one have to agree: a key the
// example shows and nothing reads is worse than no key.
//
// The file is `key: value` lines, parsed by hand (TODO/ACTIVE.md, "Decisions"): a `#`
// starts a comment, blank lines are skipped, keys are dotted (led.path), values are bare
// or quoted, and a key this daemon does not read is an error naming the line.
class Config {
 public:
  explicit Config(std::shared_ptr<loggers::Logger> logger);

  // --- log -------------------------------------------------------------------------

  loggers::LogLevel log_level = loggers::LogLevel::info;

  // --- state -----------------------------------------------------------------------

  // Where the machine id lives (docs/protocol.md, "The machine ID"). The unit's
  // StateDirectory= makes it; relative to the configuration file otherwise, so a
  // development configuration can keep its id beside itself.
  std::string state_dir = "/var/lib/rpiledconfig";

  // --- led -------------------------------------------------------------------------

  // sysfs drives the LED through /sys/class/leds; fake records what it would have done.
  // The default is the real one where there is one.
  std::string led_driver = default_led_driver;

  // The LED's sysfs node. ACT (led0 on older boards) is the default, the main LED a user
  // sees on the board; PWR (led1) is the power LED.
  std::string led_path = "/sys/class/leds/ACT";

  // Whether the LED lights when its brightness is 0. On a Raspberry Pi 5 the ACT LED
  // lit for 0 and went dark for max_brightness, so the camera saw every frame inverted.
  bool led_active_low = false;

  // The symbol time T in milliseconds. The protocol's is 45 (docs/protocol.md, Symbols);
  // any other value is an experiment the app has to be built to match (TODO/ACTIVE.md).
  std::uint32_t led_symbol_ms = 45;

  // --- ble -------------------------------------------------------------------------

  // bluez talks to BlueZ over D-Bus (milestone 2); fake is a harness for the tests and
  // the development Mac.
  std::string ble_driver = default_ble_driver;
  std::string ble_adapter = "hci0";

  // --- wifi ------------------------------------------------------------------------

  // networkmanager talks to NetworkManager over D-Bus (milestone 3); fake answers from a
  // configured list.
  std::string wifi_driver = default_wifi_driver;

  // The WiFi interface: whose wpa_supplicant control socket Wi-Fi Easy Connect uses
  // (/run/wpa_supplicant/<interface>) and whose MAC goes in its bootstrap URI.
  std::string wifi_interface = "wlan0";

  // Wi-Fi Easy Connect (docs/protocol.md): wpa_supplicant (the real enrollee, Linux
  // only), fake (answers a made-up URI and never receives anything), or off (the app is
  // told easyconnect_unavailable). Unset, it follows wifi.driver: fake with the fake WiFi
  // backend, so that a Pi run with wifi.driver: fake never touches its radio, and
  // wpa_supplicant otherwise. Resolved by loading, never empty after it.
  std::string wifi_easyconnect;

  // --- uninstall -------------------------------------------------------------------

  // What an authenticated phone's uninstall does (docs/protocol.md, "Uninstalling"):
  // systemd starts the package's rpiledconfig-uninstall.service, which purges it; fake
  // logs and removes nothing; off answers uninstall_unavailable.
  std::string uninstall_driver = default_uninstall_driver;

  // --- setup -----------------------------------------------------------------------

  // Stay in setup mode for ever, finish or no finish: for a bench Pi whose LED and BLE
  // are what is being developed against.
  bool setup_always = false;

  // Seconds in setup mode before giving up until the next recheck, and seconds between
  // WiFi checks outside it (docs/protocol.md, "Setup mode").
  std::uint32_t setup_timeout = 900;
  std::uint32_t setup_recheck = 60;

  // --- loading ---------------------------------------------------------------------

  // False, with the reason logged, for a file that cannot be read, has a line that is not
  // `key: value`, names a key nothing reads, or says something this daemon cannot run
  // with. A daemon never starts on half a configuration.
  bool load_from_file(const std::string& filename);

  // The same parse from a string, with relative paths resolved against base_dir. The
  // tests use it; so could anything that builds a configuration in memory.
  bool load_from_string(const std::string& text, const std::filesystem::path& base_dir);

  // Where the configuration is looked for, in the order a deployment wants: what the
  // command line said, then the environment, then where a package installs it.
  static std::filesystem::path locate(const std::string& from_command_line);

  static constexpr const char* environment_variable = "RPILEDCONFIG_CONFIG";
  static constexpr const char* system_path = "/etc/rpiledconfig/rpiledconfig.conf";

  // The real drivers where they exist (Linux), the fakes everywhere else: a configuration
  // with no driver keys does the right thing on both the Pi and the development Mac.
#if defined(__linux__)
  static constexpr const char* default_led_driver = "sysfs";
  static constexpr const char* default_ble_driver = "bluez";
  static constexpr const char* default_wifi_driver = "networkmanager";
  static constexpr const char* default_uninstall_driver = "systemd";
#else
  static constexpr const char* default_led_driver = "fake";
  static constexpr const char* default_ble_driver = "fake";
  static constexpr const char* default_wifi_driver = "fake";
  static constexpr const char* default_uninstall_driver = "fake";
#endif

 private:
  bool _apply(const std::string& key, const std::string& value, int line, const std::filesystem::path& base_dir);
  bool _validate();

  std::shared_ptr<loggers::Logger> _logger;
};

}  // namespace rpiledconfig
