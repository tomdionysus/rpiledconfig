//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "setup_mode.h"

#include <utility>
#include <vector>

namespace rpiledconfig::setup {

namespace {

// The session's view of one connection: the BLE service, narrowed to one id.
class BleConnection : public session::Connection {
 public:
  BleConnection(std::shared_ptr<ble::BleService> ble, ble::ConnectionId id) : _ble(std::move(ble)), _id(id) {}

  void notify(ble::Characteristic characteristic, types::Bytes value) override { _ble->notify(_id, characteristic, std::move(value)); }
  void disconnect() override { _ble->disconnect(_id); }
  std::size_t mtu() const override { return _ble->mtu(_id); }
  std::string describe() const override { return "connection " + std::to_string(_id); }

 private:
  std::shared_ptr<ble::BleService> _ble;
  ble::ConnectionId _id;
};

}  // namespace

SetupMode::SetupMode(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<timers::TimerSource> timers,
                     std::shared_ptr<led::LedModulator> modulator, std::shared_ptr<ble::BleService> ble, std::shared_ptr<wifi::Wifi> wifi,
                     std::shared_ptr<session::RequestHandler> requests, Identity identity, Options options)
    : _logger(std::move(logger)),
      _executor(std::move(executor)),
      _timers(std::move(timers)),
      _modulator(std::move(modulator)),
      _ble(std::move(ble)),
      _wifi(std::move(wifi)),
      _requests(std::move(requests)),
      _identity(std::move(identity)),
      _options(options) {}

void SetupMode::start() {
  if (_started) return;
  _started = true;

  if (_options.always) return _enter("setup.always is on");

  _check();
}

void SetupMode::stop() {
  if (!_started) return;
  _started = false;

  if (_recheck) _recheck->cancel();
  _recheck.reset();
  if (_pause) _pause->cancel();
  _pause.reset();
  _paused = false;

  _leave("stopping");
}

void SetupMode::_check() {
  if (!_started || _active) return;

  _wifi->status(_executor, [weak = weak_from_this()](types::Result<wifi::WifiStatus> result) {
    auto self = weak.lock();
    if (!self || !self->_started || self->_active) return;

    if (!result) return self->_enter("WiFi cannot be checked: " + result.error);
    if (!result.value.connected) return self->_enter("no WiFi connection");

    self->_logger->info("WiFi is connected to " + result.value.ssid + " (" + result.value.ip + "); next check in " +
                        std::to_string(self->_options.recheck.count()) + " s");
    self->_schedule_recheck();
  });
}

void SetupMode::_enter(const std::string& why) {
  if (_active) return;
  _active = true;

  _logger->info("Entering setup mode: " + why);

  // The LED first, because it is the factor a phone needs before BLE is any use. A refusal
  // is logged and setup mode goes on without it: a Pi whose udev rule is missing still
  // answers a phone that already knows its id.
  if (const auto started = _modulator->start(); !started) _logger->error("The LED cannot be driven: " + started.error);

  if (const auto started = _ble->start(ble::Advertisement{ble::local_name(_identity.fingerprint)}, weak_from_this()); !started) {
    _logger->error("BLE cannot advertise: " + started.error);
  }

  if (!_options.always) {
    _timeout = _timers->schedule(_options.timeout, [weak = weak_from_this()]() {
      auto self = weak.lock();
      if (!self) return;
      self->_leave("setup.timeout (" + std::to_string(self->_options.timeout.count()) + " s) passed");
    });
  }
}

void SetupMode::_leave(const std::string& why) {
  if (!_active) return;
  _active = false;

  _logger->info("Leaving setup mode: " + why);

  if (_timeout) _timeout->cancel();
  _timeout.reset();

  for (auto& [id, session] : _sessions) session->close();
  _sessions.clear();

  _ble->stop();
  _modulator->stop();

  _schedule_recheck();
}

void SetupMode::_schedule_recheck() {
  if (!_started) return;

  _recheck = _timers->schedule(_options.recheck, [weak = weak_from_this()]() {
    auto self = weak.lock();
    if (!self) return;
    self->_check();
  });
}

void SetupMode::_on_authenticated() { _consecutive_refusals = 0; }

void SetupMode::_on_refused() {
  ++_consecutive_refusals;
  if (_paused || _consecutive_refusals < _options.refusals_to_pause) return;

  _paused = true;
  _ble->advertising_pause();
  _logger->warn(std::to_string(_consecutive_refusals) + " refused attempts in a row; advertising paused for " +
                std::to_string(_options.advertising_pause.count()) + " s (the LED keeps blinking)");

  _pause = _timers->schedule(_options.advertising_pause, [weak = weak_from_this()]() {
    auto self = weak.lock();
    if (!self) return;

    // The count starts again: another five, another pause.
    self->_paused = false;
    self->_consecutive_refusals = 0;
    if (self->_active) {
      self->_ble->advertising_resume();
      self->_logger->info("Advertising resumed");
    }
  });
}

void SetupMode::_on_finished() {
  if (_options.always) {
    _logger->info("finish received; setup.always keeps setup mode on");
    return;
  }

  _leave("finish received");
}

void SetupMode::on_connect(ble::ConnectionId connection) {
  if (!_active) {
    _ble->disconnect(connection);
    return;
  }

  session::PairingSession::Options options;
  options.w = _identity.w;
  options.fingerprint = _identity.fingerprint;
  options.version = _identity.version;
  options.auth_timeout = _options.auth_timeout;
  options.max_refusals = _options.max_refusals;

  session::SessionEvents events;
  events.authenticated = [weak = weak_from_this()]() {
    if (auto self = weak.lock()) self->_on_authenticated();
  };
  events.refused = [weak = weak_from_this()]() {
    if (auto self = weak.lock()) self->_on_refused();
  };
  events.finished = [weak = weak_from_this()]() {
    if (auto self = weak.lock()) self->_on_finished();
  };

  auto session = std::make_shared<session::PairingSession>(_logger, _executor, _timers, std::make_shared<BleConnection>(_ble, connection), _requests,
                                                           std::move(options), std::move(events));
  _sessions[connection] = session;
  session->start();
}

void SetupMode::on_disconnect(ble::ConnectionId connection) {
  auto it = _sessions.find(connection);
  if (it == _sessions.end()) return;

  _logger->info("Disconnected: connection " + std::to_string(connection));
  it->second->close();
  _sessions.erase(it);
}

void SetupMode::on_write(ble::ConnectionId connection, ble::Characteristic characteristic, types::Bytes value) {
  auto it = _sessions.find(connection);
  if (it == _sessions.end()) return;

  it->second->write(characteristic, std::move(value));
}

types::Bytes SetupMode::on_read(ble::ConnectionId connection, ble::Characteristic characteristic) {
  auto it = _sessions.find(connection);
  if (it == _sessions.end()) return {};

  return it->second->read(characteristic);
}

}  // namespace rpiledconfig::setup
