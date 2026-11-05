//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <string>

#include "ble/ble_service.h"
#include "types/status.h"

namespace rpiledconfig::session {

// One phone's link, as the pairing session sees it: it can notify, hang up, and ask how
// big a notification may be. Setup mode makes one of these over the BLE service for each
// connection; a test makes one that records.
class Connection {
 public:
  virtual ~Connection() = default;

  virtual void notify(ble::Characteristic characteristic, types::Bytes value) = 0;
  virtual void disconnect() = 0;

  // The ATT MTU; a notification carries 3 bytes less.
  virtual std::size_t mtu() const = 0;

  virtual std::string describe() const = 0;
};

}  // namespace rpiledconfig::session
