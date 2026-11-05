//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "types/status.h"

namespace rpiledconfig::ble {

// The GATT service of docs/protocol.md, "BLE".
inline constexpr char service_uuid[] = "2a7f0001-7e1d-4c3a-9b5e-0f8a6d4c2b10";

// In the order of their UUIDs; blink came with protocol 3.
enum class Characteristic { info, challenge, auth, auth_result, request, response, blink };

const char* uuid(Characteristic characteristic);
const char* name(Characteristic characteristic);

// The local name: <prefix>-<suffix> (docs/protocol.md, BLE).
std::string local_name(const std::string& prefix, const std::string& suffix);

// One phone's connection, numbered by the service.
using ConnectionId = std::uint64_t;

// The default ATT MTU, before any negotiation: writes and notifications carry 20 bytes.
inline constexpr std::size_t default_mtu = 23;

// The largest MTU fragments are sized for, whatever is reported (docs/protocol.md,
// Fragments): 182-byte notifications reach any central that settled on 185 or more.
inline constexpr std::size_t max_fragment_mtu = 185;

// What the service tells its owner. Called on the owner's executor, except on_read, which
// is answered inline because a read is a method call the stack is waiting to reply to.
class BleListener {
 public:
  virtual ~BleListener() = default;

  virtual void on_connect(ConnectionId connection) = 0;
  virtual void on_disconnect(ConnectionId connection) = 0;
  virtual void on_write(ConnectionId connection, Characteristic characteristic, types::Bytes value) = 0;
  virtual types::Bytes on_read(ConnectionId connection, Characteristic characteristic) = 0;
};

struct Advertisement {
  std::string local_name;
};

// A BLE stack: the service registered and advertised while setup mode is on, and stopped
// when it is off. Everything a phone does arrives through the listener; everything the
// daemon does goes through the methods below.
class BleService {
 public:
  virtual ~BleService() = default;

  // Registers the service and starts advertising. The listener is kept weakly: the owner
  // outlives the advertisement.
  virtual types::Status start(const Advertisement& advertisement, std::weak_ptr<BleListener> listener) = 0;

  // Stops advertising, drops every connection and unregisters the service.
  virtual void stop() = 0;

  // Advertising alone, for the pause after five refused attempts: connections stay up and
  // the service stays registered.
  virtual void advertising_pause() = 0;
  virtual void advertising_resume() = 0;

  virtual void notify(ConnectionId connection, Characteristic characteristic, types::Bytes value) = 0;
  virtual void disconnect(ConnectionId connection) = 0;

  // The ATT MTU of a connection; a write or notification carries 3 bytes less.
  virtual std::size_t mtu(ConnectionId connection) const = 0;

  virtual std::string describe() const = 0;
};

}  // namespace rpiledconfig::ble
