//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#if RPILEDCONFIG_HAVE_SD_BUS

#include "uninstaller_systemd.h"

#include <utility>

#include "protocol/messages.h"

namespace rpiledconfig::system {

UninstallerSystemd::UninstallerSystemd(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop)
    : _logger(std::move(logger)), _loop(std::move(loop)) {}

std::string UninstallerSystemd::describe() const { return std::string("systemd (") + uninstall_unit + ")"; }

void UninstallerSystemd::start(types::Executor on, types::StatusHandler done) {
  const auto unavailable = [](const std::string& why) { return types::Status::failure(protocol::errors::uninstall_unavailable, why); };

  auto message = _loop->method_call(systemd_service, systemd_path, manager_interface, "StartUnit");
  if (!message || sd_bus_message_append(message.get(), "ss", uninstall_unit, "replace") < 0) {
    return types::complete(std::move(on), std::move(done), unavailable("cannot ask systemd: the bus is closed"));
  }

  _logger->warn(std::string("Starting ") + uninstall_unit + ": RPiLEDConfig is about to be removed from this Pi");

  _loop->call(std::move(message), [logger = _logger, on, done, unavailable](sd_bus_message* reply, const dbus::CallError* error) {
    if (error) {
      // NoSuchUnit is a daemon that was not installed from the package; AccessDenied (or
      // polkit's refusal) is a package whose polkit rule is missing.
      std::string why = "systemd refused to start " + std::string(uninstall_unit) + ": " + error->describe();
      if (error->name == "org.freedesktop.systemd1.NoSuchUnit") {
        why = std::string(uninstall_unit) + " does not exist: this daemon was not installed from the rpiledconfig package";
      }
      logger->error(why);
      return types::complete(on, done, unavailable(why));
    }

    const char* job = nullptr;
    if (sd_bus_message_read(reply, "o", &job) >= 0 && job) logger->info(std::string("systemd queued ") + job);
    types::complete(on, done, types::Status::success());
  });
}

}  // namespace rpiledconfig::system

#endif
