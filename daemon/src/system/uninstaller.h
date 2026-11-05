//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>

#include "types/status.h"

namespace rpiledconfig::system {

// The unit the package ships to purge itself (docs/protocol.md, "Uninstalling"), which the
// polkit rule lets the service's user start and nothing else.
inline constexpr char uninstall_unit[] = "rpiledconfig-uninstall.service";

// What removes RPiLEDConfig from the Pi when an authenticated phone asks. start() answers
// once the removal has been handed to something that outlives the daemon (the daemon is
// among the things it removes), or with the reason it could not be.
class Uninstaller {
 public:
  virtual ~Uninstaller() = default;

  virtual void start(types::Executor on, types::StatusHandler done) = 0;

  virtual std::string describe() const = 0;
};

}  // namespace rpiledconfig::system
