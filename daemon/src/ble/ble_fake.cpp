//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "ble_fake.h"

#include <boost/asio/post.hpp>

#include "util.h"

namespace rpiledconfig::ble {

BleFake::BleFake(std::shared_ptr<loggers::Logger> logger, types::Executor executor) : _logger(std::move(logger)), _executor(std::move(executor)) {}

types::Status BleFake::start(const Advertisement& advertisement, std::weak_ptr<BleListener> listener) {
  if (!_start_error.empty()) return types::Status::failure(_start_error);

  _advertisement = advertisement;
  _listener = std::move(listener);
  _started = true;
  _paused = false;
  ++_starts;

  _logger->info("Advertising " + advertisement.local_name + " (fake: nothing is on the air)");
  return types::Status::success();
}

void BleFake::stop() {
  if (!_started) return;

  _started = false;
  _paused = false;
  ++_stops;

  // Every phone sees the link drop, as it would when the service goes away.
  for (auto& [id, connection] : _connections) {
    if (connection.open) disconnect(id);
  }

  _logger->info("Stopped advertising");
}

void BleFake::advertising_pause() {
  if (_started) _paused = true;
}

void BleFake::advertising_resume() {
  if (_started) _paused = false;
}

void BleFake::notify(ConnectionId connection, Characteristic characteristic, types::Bytes value) {
  auto it = _connections.find(connection);
  if (it == _connections.end() || !it->second.open) return;

  _logger->debug("Notify " + std::string(name(characteristic)) + " on " + std::to_string(connection) + ": " + util::to_hex(value));
  it->second.notifications.push_back(Notification{characteristic, std::move(value)});
}

void BleFake::disconnect(ConnectionId connection) {
  auto it = _connections.find(connection);
  if (it == _connections.end() || !it->second.open) return;

  it->second.open = false;

  // The listener hears about it the same way whoever hung up, as it would from BlueZ.
  if (auto listener = _listener.lock()) {
    boost::asio::post(_executor, [listener, connection]() { listener->on_disconnect(connection); });
  }
}

std::size_t BleFake::mtu(ConnectionId connection) const {
  const auto it = _connections.find(connection);
  return it == _connections.end() ? default_mtu : it->second.mtu;
}

std::size_t BleFake::connections() const {
  std::size_t open = 0;
  for (const auto& [id, connection] : _connections) {
    if (connection.open) ++open;
  }
  return open;
}

ConnectionId BleFake::phone_connect(std::size_t mtu) {
  if (!advertising()) return 0;

  const auto id = _next_connection++;
  _connections[id].mtu = mtu;

  if (auto listener = _listener.lock()) {
    boost::asio::post(_executor, [listener, id]() { listener->on_connect(id); });
  }

  return id;
}

void BleFake::phone_disconnect(ConnectionId connection) { disconnect(connection); }

types::Bytes BleFake::phone_read(ConnectionId connection, Characteristic characteristic) {
  if (!connected(connection)) return {};

  auto listener = _listener.lock();
  if (!listener) return {};

  return listener->on_read(connection, characteristic);
}

void BleFake::phone_write(ConnectionId connection, Characteristic characteristic, types::Bytes value) {
  if (!connected(connection)) return;

  if (auto listener = _listener.lock()) {
    boost::asio::post(_executor, [listener, connection, characteristic, value = std::move(value)]() mutable {
      listener->on_write(connection, characteristic, std::move(value));
    });
  }
}

std::vector<BleFake::Notification> BleFake::notifications(ConnectionId connection) const {
  const auto it = _connections.find(connection);
  return it == _connections.end() ? std::vector<Notification>{} : it->second.notifications;
}

std::vector<BleFake::Notification> BleFake::take_notifications(ConnectionId connection) {
  auto it = _connections.find(connection);
  if (it == _connections.end()) return {};

  std::vector<Notification> out;
  out.swap(it->second.notifications);
  return out;
}

bool BleFake::connected(ConnectionId connection) const {
  const auto it = _connections.find(connection);
  return it != _connections.end() && it->second.open;
}

}  // namespace rpiledconfig::ble
