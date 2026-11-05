//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ble_service.h"
#include "loggers/logger.h"

namespace rpiledconfig::ble {

// A BLE stack a test drives from the phone's side: phone_connect() is a central
// connecting, phone_write() a write request, phone_read() a read, and notifications()
// what the phone's subscription received. The daemon's side sees exactly what it would
// see from BlueZ, through the listener, on its executor.
//
// On the development Mac it is also what the daemon runs with, where nobody connects.
class BleFake : public BleService {
 public:
  struct Notification {
    Characteristic characteristic;
    types::Bytes value;
  };

  BleFake(std::shared_ptr<loggers::Logger> logger, types::Executor executor);

  // --- BleService ------------------------------------------------------------------

  types::Status start(const Advertisement& advertisement, std::weak_ptr<BleListener> listener) override;
  void stop() override;
  void advertising_pause() override;
  void advertising_resume() override;
  void notify(ConnectionId connection, Characteristic characteristic, types::Bytes value) override;
  void disconnect(ConnectionId connection) override;
  std::size_t mtu(ConnectionId connection) const override;
  std::string describe() const override { return "fake"; }

  // --- The phone ---------------------------------------------------------------------
  //
  // Calls on the test's thread, which is the executor's thread too: the test's io_context
  // runs only when the test drains it, between these.

  // Nothing while not advertising: a phone cannot connect to what it cannot see. 0 then.
  ConnectionId phone_connect(std::size_t mtu = default_mtu);
  void phone_disconnect(ConnectionId connection);
  types::Bytes phone_read(ConnectionId connection, Characteristic characteristic);
  void phone_write(ConnectionId connection, Characteristic characteristic, types::Bytes value);

  // What the phone has been notified of so far, in order, and the same consumed. Kept
  // after a disconnect, because the last thing a phone hears (the finish response) is
  // sent just before the link goes.
  std::vector<Notification> notifications(ConnectionId connection) const;
  std::vector<Notification> take_notifications(ConnectionId connection);

  bool connected(ConnectionId connection) const;

  // --- What a test looks at ----------------------------------------------------------

  bool started() const { return _started; }
  bool advertising() const { return _started && !_paused; }
  const std::string& advertised_name() const { return _advertisement.local_name; }
  const std::string& adapter_alias() const { return _advertisement.adapter_alias; }
  int starts() const { return _starts; }
  int stops() const { return _stops; }

  // Open ones.
  std::size_t connections() const;

  // What start() answers; a failure stands in for BlueZ not being there.
  void fail_start(const std::string& reason) { _start_error = reason; }

 private:
  struct Connection {
    std::size_t mtu = default_mtu;
    bool open = true;
    std::vector<Notification> notifications;
  };

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::weak_ptr<BleListener> _listener;
  Advertisement _advertisement;
  std::map<ConnectionId, Connection> _connections;
  ConnectionId _next_connection = 1;
  std::string _start_error;
  bool _started = false;
  bool _paused = false;
  int _starts = 0;
  int _stops = 0;
};

}  // namespace rpiledconfig::ble
