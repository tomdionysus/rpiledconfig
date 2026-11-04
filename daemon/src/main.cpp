//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include <unistd.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include "ble/ble_fake.h"
#include "ble/ble_service.h"
#include "build_version.h"
#include "cli.h"
#include "config.h"
#include "global_io_context.h"
#include "led/led.h"
#include "led/led_fake.h"
#include "led/led_modulator.h"
#include "led/led_sysfs.h"
#include "loggers/logger_scoped.h"
#include "loggers/logger_stdio.h"
#include "machine_id/machine_id.h"
#include "protocol/keys.h"
#include "protocol/spake2.h"
#include "protocol/vectors.h"
#include "session/requests.h"
#include "setup/setup_mode.h"
#include "system/uninstaller.h"
#include "system/uninstaller_fake.h"
#include "timers/timer_source.h"
#include "wifi/easy_connect.h"
#include "wifi/easy_connect_fake.h"
#include "wifi/wifi.h"
#include "wifi/wifi_fake.h"

#if defined(__linux__)
#include "wifi/easy_connect_wpa_supplicant.h"
#endif

#if RPILEDCONFIG_HAVE_SD_BUS
#include "ble/ble_bluez.h"
#include "dbus/sd_bus_loop.h"
#include "system/uninstaller_systemd.h"
#include "wifi/wifi_networkmanager.h"
#endif

using namespace rpiledconfig;

namespace {

std::string hostname() {
  char buffer[256];
  if (::gethostname(buffer, sizeof(buffer)) != 0) return "raspberrypi";
  buffer[sizeof(buffer) - 1] = '\0';
  return buffer;
}

std::shared_ptr<loggers::Logger> scoped(const std::shared_ptr<loggers::Logger>& logger, const std::string& scope) {
  return std::make_shared<loggers::LoggerScoped>(scope, logger);
}

// The drivers the configuration names. A driver this build does not have (bluez and
// networkmanager without libsystemd) is refused here with the reason, rather than quietly
// replaced by a fake that would blink and advertise and never configure anything.

std::shared_ptr<led::Led> make_led(const std::shared_ptr<loggers::Logger>& logger, const Config& config) {
  if (config.led_driver == "sysfs") return std::make_shared<led::LedSysfs>(scoped(logger, "led"), config.led_path, config.led_active_low);
  return std::make_shared<led::LedFake>();
}

#if RPILEDCONFIG_HAVE_SD_BUS
using Bus = std::shared_ptr<dbus::SdBusLoop>;
#else
using Bus = std::shared_ptr<void>;
#endif

std::shared_ptr<ble::BleService> make_ble(const std::shared_ptr<loggers::Logger>& logger, const Config& config, types::Executor executor, const Bus& bus) {
  if (config.ble_driver == "fake") return std::make_shared<ble::BleFake>(scoped(logger, "ble"), std::move(executor));

#if RPILEDCONFIG_HAVE_SD_BUS
  return std::make_shared<ble::BleBluez>(scoped(logger, "ble"), bus, config.ble_adapter);
#else
  (void)bus;
  logger->error("ble.driver is bluez, but this build has no sd-bus (libsystemd was not found, or this is not Linux); use ble.driver: fake");
  return nullptr;
#endif
}

std::shared_ptr<wifi::Wifi> make_wifi(const std::shared_ptr<loggers::Logger>& logger, const Config& config, const Bus& bus) {
  if (config.wifi_driver == "fake") {
    // A run with the fake is someone trying the app against a Pi whose real network must
    // not be touched, so it has something to show: made-up networks, any passphrase joins.
    auto fake = std::make_shared<wifi::WifiFake>(scoped(logger, "wifi"));
    fake->set_networks({{"Fake Home", 82, true}, {"Fake Cafe", 55, false}, {"Fake Neighbour", 31, true}});
    return fake;
  }

#if RPILEDCONFIG_HAVE_SD_BUS
  return std::make_shared<wifi::WifiNetworkManager>(scoped(logger, "wifi"), bus);
#else
  (void)bus;
  logger->error("wifi.driver is networkmanager, but this build has no sd-bus (libsystemd was not found, or this is not Linux); use wifi.driver: fake");
  return nullptr;
#endif
}

// Wi-Fi Easy Connect: off is no enrollee at all (the app is told easyconnect_unavailable),
// not an error; wpa_supplicant on a build that is not Linux is.
bool make_easyconnect(const std::shared_ptr<loggers::Logger>& logger, const Config& config, const types::Executor& executor,
                      const std::shared_ptr<timers::TimerSource>& timers, std::shared_ptr<wifi::EasyConnect>& out) {
  if (config.wifi_easyconnect == "off") return true;

  if (config.wifi_easyconnect == "fake") {
    out = std::make_shared<wifi::EasyConnectFake>(scoped(logger, "easyconnect"));
    return true;
  }

#if defined(__linux__)
  wifi::EasyConnectWpaSupplicant::Options options;
  options.interface = config.wifi_interface;
  out = std::make_shared<wifi::EasyConnectWpaSupplicant>(scoped(logger, "easyconnect"), executor, timers, options);
  return true;
#else
  (void)executor;
  (void)timers;
  logger->error("wifi.easyconnect is wpa_supplicant, which needs Linux; use wifi.easyconnect: fake or off");
  return false;
#endif
}

// Uninstalling from the phone: off is no uninstaller (the app is told
// uninstall_unavailable), not an error; systemd on a build without sd-bus is.
bool make_uninstaller(const std::shared_ptr<loggers::Logger>& logger, const Config& config, const Bus& bus,
                      std::shared_ptr<system::Uninstaller>& out) {
  if (config.uninstall_driver == "off") return true;

  if (config.uninstall_driver == "fake") {
    out = std::make_shared<system::UninstallerFake>(scoped(logger, "uninstall"));
    return true;
  }

#if RPILEDCONFIG_HAVE_SD_BUS
  out = std::make_shared<system::UninstallerSystemd>(scoped(logger, "uninstall"), bus);
  return true;
#else
  (void)bus;
  logger->error("uninstall.driver is systemd, but this build has no sd-bus (libsystemd was not found, or this is not Linux); use uninstall.driver: fake or off");
  return false;
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto options = cli::parse(argc, argv);

  // Answered before anything is started, and on stdout rather than through the logger: a
  // person or a script asked a question, and the answer is the whole output.
  if (!options.ok) {
    std::cerr << "rpiledconfig: " << options.error << "\n\n" << cli::usage();
    return 2;
  }

  if (options.version) {
    std::cout << RPILEDCONFIG_VERSION_STRING << "\n";
    return 0;
  }

  if (options.help) {
    std::cout << cli::usage();
    return 0;
  }

  if (options.print_vectors) {
    std::cout << protocol::vectors::print(protocol::vectors::compute());
    return 0;
  }

  // Info until the configuration says otherwise, which it cannot say until it is read.
  auto logger = std::make_shared<loggers::LoggerStdIO>(loggers::LogLevel::info);

  // --- Configuration ---

  const auto config_path = Config::locate(options.config);
  auto config = std::make_shared<Config>(logger);
  if (!config->load_from_file(config_path.string())) {
    logger->error("Cannot start with the configuration in " + config_path.string());
    return 1;
  }

  // --- The machine id, on its own for --show-id and --regenerate-id ---

  if (options.show_id) {
    const auto id = machine_id::load(config->state_dir);
    if (!id) {
      std::cerr << "rpiledconfig: no machine id yet (" << id.error << "); it is made when the package is installed, or on the daemon's first start\n";
      return 3;
    }
    std::cout << id.value.to_hex() << "\n";
    return 0;
  }

  if (options.regenerate_id) {
    const auto id = machine_id::generate(config->state_dir);
    if (!id) {
      std::cerr << "rpiledconfig: cannot regenerate the machine id: " << id.error << "\n";
      return 3;
    }
    std::cout << id.value.to_hex() << "\n";
    return 0;
  }

  const std::string title = " RPiLEDConfig " RPILEDCONFIG_VERSION_STRING " ";
  logger->raw(std::string(title.size(), '-'));
  logger->raw(title);
  logger->raw(std::string(title.size(), '-'));

  logger->info("Configuration: " + config_path.string());
  logger->set_level(config->log_level);
  logger->info("Log level " + loggers::to_string(config->log_level) + ", state in " + config->state_dir);

  const auto id = machine_id::load_or_generate(config->state_dir);
  if (!id) {
    logger->error("Cannot start without a machine id: " + id.error);
    return 3;
  }

  setup::SetupMode::Identity identity;
  identity.id = id.value;
  identity.w = protocol::password_scalar(id.value);
  identity.fingerprint = protocol::fingerprint(id.value);
  identity.version = RPILEDCONFIG_VERSION_STRING;

  if (identity.w.size() != protocol::spake2::scalar_length) {
    logger->error("Cannot derive the SPAKE2 password scalar (OpenSSL refused)");
    return 3;
  }

  // Never the id itself: it is the whole pairing secret, and the journal is readable by
  // more than root (adm, systemd-journal). --show-id prints it for whoever can read the state.
  logger->info("Fingerprint " + identity.fingerprint + ", BLE name " + ble::local_name(identity.fingerprint));

  // --- The setup strand ---
  //
  // Setup mode, the sessions, the modulator and the drivers' callbacks all live here. One
  // strand, so none of them needs a lock, and the timer source is made for it.
  types::Executor executor = boost::asio::make_strand(detail::get_global_io_context());
  auto timers = std::make_shared<timers::AsioTimerSource>(executor);

  // The system bus, when a driver talks over it: one connection, pumped on the same strand,
  // shared by BlueZ and NetworkManager.
  Bus bus;
#if RPILEDCONFIG_HAVE_SD_BUS
  if (config->ble_driver != "fake" || config->wifi_driver != "fake" || config->uninstall_driver == "systemd") {
    bus = std::make_shared<dbus::SdBusLoop>(scoped(logger, "dbus"), executor);

    types::Status opened = types::Status::failure("not run");
    detail::run_and_wait(executor, [&]() { opened = bus->open(); });
    if (!opened) {
      logger->error("Cannot start without the system bus: " + opened.error);
      return 1;
    }
  }
#endif

  auto led_driver = make_led(logger, *config);
  auto ble_driver = make_ble(logger, *config, executor, bus);
  auto wifi_driver = make_wifi(logger, *config, bus);

  std::shared_ptr<wifi::EasyConnect> easyconnect;
  const bool easyconnect_made = make_easyconnect(logger, *config, executor, timers, easyconnect);

  std::shared_ptr<system::Uninstaller> uninstaller;
  const bool uninstaller_made = make_uninstaller(logger, *config, bus, uninstaller);

  if (!led_driver || !ble_driver || !wifi_driver || !easyconnect_made || !uninstaller_made) {
    logger->error("Cannot start with the drivers in " + config_path.string());
    return 1;
  }

  logger->info("LED: " + led_driver->describe() + "; BLE: " + ble_driver->describe() + "; WiFi: " + wifi_driver->describe() +
               "; Wi-Fi Easy Connect: " + (easyconnect ? easyconnect->describe() : std::string("off")) +
               "; uninstall: " + (uninstaller ? uninstaller->describe() : std::string("off")));

  auto modulator = std::make_shared<led::LedModulator>(scoped(logger, "led"), executor, led_driver, timers, id.value, std::chrono::milliseconds(config->led_symbol_ms));
  auto requests = std::make_shared<session::Requests>(scoped(logger, "requests"), wifi_driver, hostname(), easyconnect, uninstaller);

  setup::SetupMode::Options setup_options;
  setup_options.always = config->setup_always;
  setup_options.timeout = std::chrono::seconds(config->setup_timeout);
  setup_options.recheck = std::chrono::seconds(config->setup_recheck);

  auto setup_mode = std::make_shared<setup::SetupMode>(scoped(logger, "setup"), executor, timers, modulator, ble_driver, wifi_driver, requests,
                                                       std::move(identity), setup_options);

  boost::asio::post(executor, [setup_mode]() { setup_mode->start(); });

  logger->info("Started");

  // --- Waiting to stop ---
  //
  // SIGTERM is how systemd stops a service and SIGINT is Ctrl-C; both stop cleanly, which
  // matters here: the LED's trigger has to be given back. SIGHUP is taken and logged
  // rather than left to its default, which would kill the process: there is no
  // configuration reload yet, and a stray HUP from a closed terminal should not take the
  // daemon down.
  boost::asio::io_context signal_context;
  boost::asio::signal_set signals(signal_context, SIGINT, SIGTERM, SIGHUP);

  std::function<void()> wait_for_signal;
  wait_for_signal = [&]() {
    signals.async_wait([&](const boost::system::error_code& ec, int signal_number) {
      if (ec) return;

      if (signal_number == SIGHUP) {
        logger->info("SIGHUP received; this version does not reload its configuration");
        return wait_for_signal();
      }

      logger->info(std::string(signal_number == SIGTERM ? "SIGTERM" : "SIGINT") + " received; stopping");
      signal_context.stop();
    });
  };

  wait_for_signal();
  signal_context.run();

  // On the strand, and waited for: the LED's trigger goes back and the advertisement comes
  // down before the process leaves.
  //
  // Then the bus, on the strand too: closing flushes the unregistrations stop() queued, and
  // after it nothing touches the connection from any thread.
  detail::run_and_wait(executor, [setup_mode, &bus]() {
    setup_mode->stop();
#if RPILEDCONFIG_HAVE_SD_BUS
    if (bus) bus->close();
#else
    (void)bus;
#endif
  });

  logger->info("Stopped");
  return 0;
}
