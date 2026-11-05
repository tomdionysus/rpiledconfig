//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ble/ble_service.h"
#include "connection.h"
#include "loggers/logger.h"
#include "protocol/channel.h"
#include "protocol/fragments.h"
#include "request_handler.h"
#include "timers/timer_source.h"
#include "types/status.h"

namespace rpiledconfig::session {

// What a session tells setup mode, which counts refusals across connections and leaves
// on finish.
struct SessionEvents {
  std::function<void()> authenticated;
  std::function<void()> refused;
  std::function<void()> finished;
  std::function<void()> uninstalling;
};

// One connection's state machine (docs/protocol.md, "Authentication" and after): a
// challenge, up to three attempts at it, then the encrypted channel and its requests.
// Driven by whatever owns the BLE service: reads and writes come in, notifications and a
// disconnect go out through the Connection. Everything happens on one executor, and the
// timer source is the one made for it.
class PairingSession : public std::enable_shared_from_this<PairingSession> {
 public:
  struct Options {
    // w, the SPAKE2 password scalar (protocol::password_scalar).
    types::Bytes w;
    std::string version;

    // What Info says of the LED, which setup mode knows. Unset: off.
    std::function<std::string()> led_state;

    // Dropped if not authenticated by then.
    std::chrono::milliseconds auth_timeout{30000};

    // Dropped on this many refused attempts.
    int max_refusals = 3;
  };

  enum class State { unauthenticated, authenticated, closed };

  PairingSession(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<timers::TimerSource> timers,
                 std::shared_ptr<Connection> connection, std::shared_ptr<RequestHandler> requests, Options options, SessionEvents events);

  // Draws y and pB and starts the authentication clock.
  void start();

  // A read of Info, Challenge or AuthResult. Empty for anything else, and for AuthResult
  // before any attempt. A read of Challenge once authenticated starts the session over.
  types::Bytes read(ble::Characteristic characteristic);

  // A write of Auth or Request. Anything else is ignored.
  void write(ble::Characteristic characteristic, types::Bytes value);

  // The link is gone, or setup mode is ending: nothing more happens here.
  void close();

  State state() const { return _state; }
  int refusals() const { return _refusals; }

 private:
  void _auth(const types::Bytes& value);
  void _request(const types::Bytes& fragment);
  void _dispatch(const types::Bytes& plaintext);
  void _send(const std::string& json);
  void _drop(const std::string& why);
  void _renew_challenge();
  void _start_over();

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::shared_ptr<timers::TimerSource> _timers;
  std::shared_ptr<Connection> _connection;
  std::shared_ptr<RequestHandler> _requests;
  Options _options;
  SessionEvents _events;

  State _state = State::unauthenticated;
  // This attempt's SPAKE2 scalar and the share made from it, which Challenge returns.
  types::Bytes _y;
  types::Bytes _p_b;
  types::Bytes _auth_result;
  int _refusals = 0;
  std::optional<protocol::Channel> _channel;
  protocol::Reassembler _reassembler;
  bool _busy = false;
  // Whether a request reached the handler, so that closing tells it (RequestHandler::closed).
  bool _requests_used = false;
  // Bumped when the session starts over, so an answer to a request from before is dropped.
  std::uint64_t _generation = 0;
  std::shared_ptr<timers::Timer> _timeout;
};

}  // namespace rpiledconfig::session
