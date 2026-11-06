//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "setup_mode.h"

#include <utility>
#include <vector>

#include "protocol/keys.h"
#include "protocol/messages.h"
#include "protocol/spake2.h"

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

  if (_options.always) return _enter("setup.always is on");

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

  // Only BLE: the LED waits for a phone to ask (docs/protocol.md, Blink), and there is no key
  // to show until one connects.
  if (const auto started =
          _ble->start(ble::Advertisement{_identity.ble_name, _options.hide_adapter_name ? _identity.ble_name : std::string()}, weak_from_this());
      !started) {
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
  _stop_blinking("setup mode is ending");
  _discard_key("setup mode is ending");

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

void SetupMode::_on_authenticated() {
  _consecutive_refusals = 0;

  // The LED has done its work: whoever authenticated read it.
  _stop_blinking("a phone authenticated");
}

void SetupMode::_blink(ble::ConnectionId connection, const types::Bytes& value) {
  if (value.size() == 1 && value[0] == 0x00) return _stop_blinking("connection " + std::to_string(connection) + " asked");

  const auto symbol = value.size() == 1 ? led::blink_rate(value[0]) : std::nullopt;
  if (!symbol) {
    _logger->debug("A Blink write that is not a rate or 0x00 ignored, from connection " + std::to_string(connection));
    return;
  }

  // Another rate is new frames from the sync, with the same key: what a phone that cannot
  // read one rate asks for next.
  if (_modulator->running() && _modulator->symbol() != *symbol) _modulator->stop();

  if (!_blinking) {
    _blinking = true;
    _logger->info("Blinking the key, as connection " + std::to_string(connection) + " asked, for up to " + std::to_string(_options.blink_timeout.count()) +
                  " s");
  } else {
    _logger->info("Blink asked for again by connection " + std::to_string(connection) + "; " + std::to_string(_options.blink_timeout.count()) + " s from now");
  }

  // Tried again on every ask, the LED having refused before. A refusal is logged and the
  // rest goes on: setup mode does not stop for an LED that cannot be driven.
  if (!_modulator->running()) {
    const auto started = _modulator->start(_key->key, *symbol);
    _led_failed = !started;
    if (!started) _logger->error("The LED cannot be driven: " + started.error);
  }

  if (_blink_timeout) _blink_timeout->cancel();
  _blink_timeout = _timers->schedule(_options.blink_timeout, [weak = weak_from_this()]() {
    auto self = weak.lock();
    if (!self) return;
    self->_stop_blinking(std::to_string(self->_options.blink_timeout.count()) + " s passed");
  });
}

void SetupMode::_stop_blinking(const std::string& why) {
  if (_blink_timeout) _blink_timeout->cancel();
  _blink_timeout.reset();

  if (!_blinking) return;
  _blinking = false;
  _led_failed = false;

  _logger->info("Stopped blinking: " + why);
  _modulator->stop();
  _arm_key_expiry();
}

bool SetupMode::_ensure_key() {
  if (_key) return true;

  const auto drawn = _identity.draw_key();
  if (!drawn) {
    _logger->error("Cannot draw a pairing key: " + drawn.error);
    return false;
  }

  auto w = protocol::password_scalar(drawn.value);
  if (w.size() != protocol::spake2::scalar_length) {
    _logger->error("Cannot derive the SPAKE2 password scalar (OpenSSL refused)");
    return false;
  }

  // Never the key itself: it is the whole pairing secret.
  _key = Key{drawn.value, std::move(w)};
  _logger->info("A new pairing key, for this pairing only");

  _key_lifetime = _timers->schedule(_options.key_lifetime, [weak = weak_from_this()]() {
    auto self = weak.lock();
    if (!self) return;

    // Whoever holds this key, on any connection, is done with it.
    const auto why = std::to_string(self->_options.key_lifetime.count()) + " s since it was drawn";
    self->_stop_blinking(why);
    self->_discard_key(why);
    std::vector<ble::ConnectionId> connections;
    for (const auto& [id, session] : self->_sessions) connections.push_back(id);
    for (const auto id : connections) self->_ble->disconnect(id);
  });
  return true;
}

void SetupMode::_discard_key(const std::string& why) {
  if (_key_expiry) _key_expiry->cancel();
  _key_expiry.reset();
  if (_key_lifetime) _key_lifetime->cancel();
  _key_lifetime.reset();

  if (!_key) return;
  _key.reset();
  _logger->info("Pairing key discarded: " + why);
}

void SetupMode::_arm_key_expiry() {
  if (_key_expiry) _key_expiry->cancel();
  _key_expiry.reset();

  // Only with nobody using it: a phone connected, or the LED still showing it.
  if (!_key || _blinking || !_sessions.empty()) return;

  _key_expiry = _timers->schedule(_options.key_idle, [weak = weak_from_this()]() {
    auto self = weak.lock();
    if (!self) return;
    self->_discard_key(std::to_string(self->_options.key_idle.count()) + " s with no phone and no blinking");
  });
}

void SetupMode::_on_refused() {
  ++_consecutive_refusals;
  if (_paused || _consecutive_refusals < _options.refusals_to_pause) return;

  _paused = true;
  _ble->advertising_pause();
  _logger->warn(std::to_string(_consecutive_refusals) + " refused attempts in a row; advertising paused for " +
                std::to_string(_options.advertising_pause.count()) + " s (a blink asked for carries on)");

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
    _stop_blinking("finish received");
    _discard_key("finish received");
    return;
  }

  _leave("finish received");
}

void SetupMode::_on_uninstalling() {
  // Not with setup.always's exception: a Pi being removed offers nothing to anyone. Should the
  // removal never stop this daemon, the next check brings setup mode back.
  _leave("the package is being removed");
}

void SetupMode::on_connect(ble::ConnectionId connection) {
  if (!_active) {
    _ble->disconnect(connection);
    return;
  }

  // The first connection of a pairing draws the key; a reconnect finds it still there.
  if (!_ensure_key()) {
    _ble->disconnect(connection);
    return;
  }
  if (_key_expiry) _key_expiry->cancel();
  _key_expiry.reset();

  session::PairingSession::Options options;
  options.w = _key->w;
  options.version = _identity.version;
  options.led_state = [weak = weak_from_this()]() -> std::string {
    auto self = weak.lock();
    if (!self || !self->_blinking) return protocol::led_states::off;
    return self->_led_failed ? protocol::led_states::failed : protocol::led_states::blinking;
  };
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
  events.uninstalling = [weak = weak_from_this()]() {
    if (auto self = weak.lock()) self->_on_uninstalling();
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
  _arm_key_expiry();
}

void SetupMode::on_write(ble::ConnectionId connection, ble::Characteristic characteristic, types::Bytes value) {
  // Blink is setup mode's, not the session's: it needs no authentication, and it outlives
  // the connection that wrote it.
  if (characteristic == ble::Characteristic::blink) {
    if (_active && _key && _sessions.count(connection) != 0) _blink(connection, value);
    return;
  }

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
