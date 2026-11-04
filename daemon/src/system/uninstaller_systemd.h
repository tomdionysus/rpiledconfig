//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#if RPILEDCONFIG_HAVE_SD_BUS

#include <memory>
#include <string>

#include "dbus/sd_bus_loop.h"
#include "loggers/logger.h"
#include "uninstaller.h"

namespace rpiledconfig::system {

// Starts rpiledconfig-uninstall.service through systemd's Manager.StartUnit, mode replace,
// on the daemon's bus connection. The unit runs as root and purges the package; the daemon
// may start it because the package's polkit rule says so. It answers when systemd has
// queued the job, which is before the unit runs: the unit waits a few seconds first, so the
// answer reaches the phone before the daemon is stopped.
class UninstallerSystemd : public Uninstaller {
 public:
  static constexpr const char* systemd_service = "org.freedesktop.systemd1";
  static constexpr const char* systemd_path = "/org/freedesktop/systemd1";
  static constexpr const char* manager_interface = "org.freedesktop.systemd1.Manager";

  UninstallerSystemd(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop);

  void start(types::Executor on, types::StatusHandler done) override;
  std::string describe() const override;

 private:
  std::shared_ptr<loggers::Logger> _logger;
  std::shared_ptr<dbus::SdBusLoop> _loop;
};

}  // namespace rpiledconfig::system

#endif
