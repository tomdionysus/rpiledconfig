//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "pairing_session.h"

#include <algorithm>
#include <utility>

#include "protocol/auth.h"
#include "protocol/crypto.h"
#include "protocol/keys.h"
#include "protocol/messages.h"
#include "protocol/spake2.h"
#include "util.h"

namespace rpiledconfig::session {

using namespace protocol;

PairingSession::PairingSession(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<timers::TimerSource> timers,
                               std::shared_ptr<Connection> connection, std::shared_ptr<RequestHandler> requests, Options options, SessionEvents events)
    : _logger(std::move(logger)),
      _executor(std::move(executor)),
      _timers(std::move(timers)),
      _connection(std::move(connection)),
      _requests(std::move(requests)),
      _options(std::move(options)),
      _events(std::move(events)) {}

void PairingSession::start() {
  _renew_challenge();

  _timeout = _timers->schedule(_options.auth_timeout, [weak = weak_from_this()]() {
    auto self = weak.lock();
    if (!self || self->_state != State::unauthenticated) return;
    self->_drop("not authenticated within " + std::to_string(self->_options.auth_timeout.count() / 1000) + " s");
  });

  _logger->info("Connected: " + _connection->describe());
}

types::Bytes PairingSession::read(ble::Characteristic characteristic) {
  if (_state == State::closed) return {};

  switch (characteristic) {
    case ble::Characteristic::info:
      return util::to_bytes(info_json(_options.version, _options.led_state ? _options.led_state() : std::string(led_states::off)));
    case ble::Characteristic::challenge:
      // docs/protocol.md: on an authenticated connection a read of Challenge starts over,
      // because an app that starts again may find the operating system kept the old link.
      if (_state == State::authenticated) _start_over();
      return _p_b;
    case ble::Characteristic::auth_result:
      return _auth_result;
    default:
      return {};
  }
}

void PairingSession::write(ble::Characteristic characteristic, types::Bytes value) {
  if (_state == State::closed) return;

  switch (characteristic) {
    case ble::Characteristic::auth:
      return _auth(value);
    case ble::Characteristic::request:
      return _request(value);
    default:
      _logger->debug("A write to " + std::string(ble::name(characteristic)) + " ignored");
  }
}

void PairingSession::close() {
  if (_state == State::closed) return;

  _state = State::closed;
  if (_timeout) _timeout->cancel();
  _timeout.reset();
  _channel.reset();

  // Whatever this connection started at the handler (Wi-Fi Easy Connect listening) ends
  // with it.
  if (_requests_used) _requests->closed();
  _requests_used = false;
}

void PairingSession::_start_over() {
  _logger->info("Challenge read after authentication: " + _connection->describe() + " starts over");

  // As on a new connection: no key, no channel, nothing half reassembled, AuthResult empty
  // until an attempt, the attempts and the clock from zero. A request still being answered
  // belongs to the session that is gone, and its answer is dropped (_generation).
  ++_generation;
  _state = State::unauthenticated;
  _channel.reset();
  _reassembler.reset();
  _auth_result.clear();
  _refusals = 0;
  _busy = false;
  if (_requests_used) _requests->closed();
  _requests_used = false;

  if (_timeout) _timeout->cancel();
  _timeout = _timers->schedule(_options.auth_timeout, [weak = weak_from_this(), generation = _generation]() {
    auto self = weak.lock();
    if (!self || self->_generation != generation || self->_state != State::unauthenticated) return;
    self->_drop("not authenticated within " + std::to_string(self->_options.auth_timeout.count() / 1000) + " s");
  });

  _renew_challenge();
}

void PairingSession::_renew_challenge() {
  // A new y for every pB: a y used twice gives the password away (RFC 9382, section 7).
  _y = spake2::random_scalar();
  _p_b = _y.empty() ? types::Bytes{} : spake2::share(spake2::Role::b, _options.w, _y);
  if (_p_b.size() != spake2::share_length) {
    _y.clear();
    _p_b.clear();
    _logger->error("No SPAKE2 share (the CSPRNG or OpenSSL failed); dropping " + _connection->describe());
    _drop("no challenge");
  }
}

void PairingSession::_auth(const types::Bytes& value) {
  if (_state != State::unauthenticated) {
    _logger->debug("An Auth write after authentication ignored");
    return;
  }

  auto keys = accept_auth(_options.w, _y, _p_b, value);

  if (keys) {
    _channel.emplace(session_key(keys.value.ke));

    _auth_result.clear();
    _auth_result.push_back(auth_accepted);
    _auth_result.insert(_auth_result.end(), keys.value.cb.begin(), keys.value.cb.end());

    // This y is spent; a Challenge read from here on starts over with a new one.
    _y.clear();

    _state = State::authenticated;
    if (_timeout) _timeout->cancel();
    _timeout.reset();

    _logger->info("Authenticated: " + _connection->describe());
    _connection->notify(ble::Characteristic::auth_result, _auth_result);
    if (_events.authenticated) _events.authenticated();
    return;
  }

  ++_refusals;
  _auth_result.assign(1, auth_refused);
  _renew_challenge();
  if (_state == State::closed) return;

  _logger->warn("Refused attempt " + std::to_string(_refusals) + " from " + _connection->describe() + ": " + keys.error);
  _connection->notify(ble::Characteristic::auth_result, _auth_result);
  if (_events.refused) _events.refused();

  if (_refusals >= _options.max_refusals) _drop(std::to_string(_refusals) + " refused attempts");
}

void PairingSession::_request(const types::Bytes& fragment) {
  if (_state != State::authenticated) return _drop("a request before authentication");

  auto pushed = _reassembler.push(fragment);
  if (!pushed) return _drop(pushed.error);
  if (!pushed.value) return;

  auto opened = _channel->open_from_app(*pushed.value);
  if (!opened) return _drop(opened.error);

  _dispatch(opened.value);
}

void PairingSession::_dispatch(const types::Bytes& plaintext) {
  auto parsed = parse_request(util::to_string(plaintext));
  if (!parsed) return _send(error_response(parsed.code, parsed.error));

  if (_busy) return _send(error_response(errors::busy, "a request is still being answered"));

  const auto op = parsed.value.op;
  _logger->info("Request " + op + " from " + _connection->describe());
  _busy = true;
  _requests_used = true;

  _requests->handle(parsed.value, _executor, [weak = weak_from_this(), op, generation = _generation](std::string json) {
    auto self = weak.lock();
    if (!self || self->_generation != generation || self->_state != State::authenticated) return;

    self->_busy = false;
    self->_send(json);

    // finish: answered, and then the daemon leaves setup mode and hangs up, in that order,
    // so the answer is on its way before the link goes.
    if (op == ops::finish) {
      self->_logger->info("Finished: " + self->_connection->describe());
      if (self->_events.finished) self->_events.finished();
      self->_drop("finished");
    }

    // uninstall: answered, and the link goes; the package's removal stops the daemon soon
    // after. Only an ok: a phone told uninstall_unavailable may still want the rest.
    if (op == ops::uninstall && json == ok_response()) {
      if (self->_events.uninstalling) self->_events.uninstalling();
      self->_drop("uninstalling");
    }
  });
}

void PairingSession::_send(const std::string& json) {
  if (_state != State::authenticated) return;

  const auto wire = _channel->seal_to_app(util::to_bytes(json));
  if (wire.empty()) return _drop("cannot seal a response");

  const std::size_t fragment_size = std::max<std::size_t>(_connection->mtu(), ble::default_mtu) - 3;
  for (auto& piece : fragment(wire, fragment_size)) _connection->notify(ble::Characteristic::response, std::move(piece));
}

void PairingSession::_drop(const std::string& why) {
  if (_state == State::closed) return;

  _logger->info("Dropping " + _connection->describe() + ": " + why);
  close();
  _connection->disconnect();
}

}  // namespace rpiledconfig::session
