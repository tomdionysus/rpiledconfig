//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "ble/ble_service.h"
#include "led/led_modulator.h"
#include "loggers/logger.h"
#include "pairing_key/pairing_key.h"
#include "session/pairing_session.h"
#include "session/request_handler.h"
#include "timers/timer_source.h"
#include "types/status.h"
#include "wifi/wifi.h"

namespace rpiledconfig::setup {

// Setup mode (docs/protocol.md, "Setup mode"): the BLE service advertising, and the LED
// blinking while a phone has asked for it (Blink). Entered at start when there is no WiFi connection, left on finish or after
// setup.timeout; outside it the daemon idles and checks WiFi every setup.recheck. One
// executor for all of it, the sessions included, and the timer source made for it.
class SetupMode : public ble::BleListener, public std::enable_shared_from_this<SetupMode> {
 public:
  struct Options {
    bool always = false;
    std::chrono::seconds timeout{900};
    std::chrono::seconds recheck{60};

    // Five refused attempts in a row across connections pause advertising for a minute.
    int refusals_to_pause = 5;
    std::chrono::seconds advertising_pause{60};

    // Per connection: see PairingSession::Options.
    std::chrono::milliseconds auth_timeout{30000};
    int max_refusals = 3;

    // How long the LED blinks after the last 0x01 written to Blink.
    std::chrono::seconds blink_timeout{120};

    // How long the key outlives the last connection and the blinking (docs/protocol.md, The
    // pairing key): long enough for a phone to finish reading the LED and come back, and for
    // the link a WiFi join dropped to return.
    std::chrono::seconds key_idle{120};

    // The most a key lasts, whatever is going on: connections alone must not keep one alive.
    std::chrono::seconds key_lifetime{900};

    // ble.hide_adapter_name: the adapter takes the advertised name while advertising.
    bool hide_adapter_name = false;
  };

  struct Identity {
    // The advertised name. Unrelated to any key.
    std::string ble_name;
    std::string version;
    std::function<types::Result<pairing_key::PairingKey>()> draw_key;
  };

  SetupMode(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<timers::TimerSource> timers,
            std::shared_ptr<led::LedModulator> modulator, std::shared_ptr<ble::BleService> ble, std::shared_ptr<wifi::Wifi> wifi,
            std::shared_ptr<session::RequestHandler> requests, Identity identity, Options options);

  // Asks the WiFi backend and enters setup mode if there is no connection. On the executor.
  void start();

  // Leaves setup mode if in it and stops the checks. On the executor.
  void stop();

  bool active() const { return _active; }
  bool advertising_paused() const { return _paused; }
  std::size_t sessions() const { return _sessions.size(); }
  int consecutive_refusals() const { return _consecutive_refusals; }
  bool blinking() const { return _blinking; }
  bool has_key() const { return _key.has_value(); }

  // --- BleListener -------------------------------------------------------------------

  void on_connect(ble::ConnectionId connection) override;
  void on_disconnect(ble::ConnectionId connection) override;
  void on_write(ble::ConnectionId connection, ble::Characteristic characteristic, types::Bytes value) override;
  types::Bytes on_read(ble::ConnectionId connection, ble::Characteristic characteristic) override;

 private:
  void _check();
  void _enter(const std::string& why);
  void _leave(const std::string& why);
  void _schedule_recheck();
  void _on_authenticated();
  void _on_refused();
  void _on_finished();
  void _on_uninstalling();
  void _blink(ble::ConnectionId connection, const types::Bytes& value);
  void _stop_blinking(const std::string& why);
  bool _ensure_key();
  void _discard_key(const std::string& why);
  void _arm_key_expiry();

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::shared_ptr<timers::TimerSource> _timers;
  std::shared_ptr<led::LedModulator> _modulator;
  std::shared_ptr<ble::BleService> _ble;
  std::shared_ptr<wifi::Wifi> _wifi;
  std::shared_ptr<session::RequestHandler> _requests;
  Identity _identity;
  Options _options;

  std::map<ble::ConnectionId, std::shared_ptr<session::PairingSession>> _sessions;
  bool _started = false;
  bool _active = false;
  bool _paused = false;
  int _consecutive_refusals = 0;
  // Asked for and not yet stopped, whether or not the LED could be driven.
  bool _blinking = false;
  // The LED refused the latest attempt to drive it.
  bool _led_failed = false;

  // The one-time key and its password scalar. From the first connection of a pairing until
  // _discard_key.
  struct Key {
    pairing_key::PairingKey key;
    types::Bytes w;
  };
  std::optional<Key> _key;

  std::shared_ptr<timers::Timer> _timeout;
  std::shared_ptr<timers::Timer> _recheck;
  std::shared_ptr<timers::Timer> _pause;
  std::shared_ptr<timers::Timer> _blink_timeout;
  std::shared_ptr<timers::Timer> _key_expiry;
  std::shared_ptr<timers::Timer> _key_lifetime;
};

}  // namespace rpiledconfig::setup
