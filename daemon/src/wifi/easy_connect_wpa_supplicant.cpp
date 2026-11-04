//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "easy_connect_wpa_supplicant.h"

#if defined(__linux__)

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <boost/asio/buffer.hpp>
#include <cerrno>
#include <cstring>
#include <utility>

#include "protocol/messages.h"
#include "wpa_control.h"

// Where NetworkManager and this may get in each other's way, for the supervised radio
// test to look at (docs/configuration.md says the same for an operator):
//
// - NetworkManager drives the same wpa_supplicant over D-Bus and asks it to scan. In
//   setup mode there is by definition no WiFi connection, so NetworkManager is scanning
//   for one to autoconnect to, every few seconds at first and then less often. A scan
//   takes the radio off channel 6 for its duration, and wpa_supplicant's radio work queue
//   runs DPP_LISTEN's remain-on-channel and the scans one after another: the listen is
//   interrupted, and the phone's DPP Authentication Request can be missed while it is.
//   The phone retries, so this costs time rather than failing outright, but it is the
//   first thing to look at if the exchange is slow or does not start.
// - An autoconnect attempt by NetworkManager while listening (to a saved network that has
//   come into range) moves the radio to that network's channel for the attempt.
// - NetworkManager does not know about the listen and does not need to: nothing here
//   changes wpa_supplicant's configuration, and dpp_config_processing stays at 0, so
//   wpa_supplicant adds no network that NetworkManager would then find and remove.
// - The Pi's brcmfmac driver has to support remain-on-channel on the station interface
//   for DPP_LISTEN to do anything; wpa_supplicant answering OK only means it was queued.

namespace rpiledconfig::wifi {

namespace {

// The `freq=` line of wpa_supplicant's STATUS (the associated channel), or 0 when it has none.
unsigned status_frequency(const std::string& status) {
  std::size_t at = 0;
  while (at < status.size()) {
    const auto end = status.find('\n', at);
    const auto line = status.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (line.rfind("freq=", 0) == 0) {
      unsigned value = 0;
      for (const char c : line.substr(5)) {
        if (c < '0' || c > '9') break;
        value = value * 10 + static_cast<unsigned>(c - '0');
      }
      return value;
    }
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return 0;
}

using protocol::errors::easyconnect_unavailable;

std::string errno_text(int error) { return std::strerror(error); }

}  // namespace

EasyConnectWpaSupplicant::EasyConnectWpaSupplicant(std::shared_ptr<loggers::Logger> logger, types::Executor executor,
                                                   std::shared_ptr<timers::TimerSource> timers, Options options)
    : _logger(std::move(logger)), _executor(executor), _timers(std::move(timers)), _options(std::move(options)), _commands(executor), _events(executor) {}

std::string EasyConnectWpaSupplicant::describe() const { return "wpa_supplicant " + socket_path(); }

types::Status EasyConnectWpaSupplicant::_open(Socket& socket) {
  const auto path = socket_path();

  sockaddr_un peer{};
  peer.sun_family = AF_UNIX;
  if (path.size() >= sizeof(peer.sun_path)) return types::Status::failure(path + " is too long for a socket path");
  std::memcpy(peer.sun_path, path.c_str(), path.size() + 1);

  const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return types::Status::failure("cannot make a socket: " + errno_text(errno));

  // An address of the family alone asks the kernel for an unused abstract name.
  sockaddr_un local{};
  local.sun_family = AF_UNIX;
  if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(sa_family_t)) < 0) {
    const int error = errno;
    ::close(fd);
    return types::Status::failure("cannot bind a client socket: " + errno_text(error));
  }

  if (::connect(fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) < 0) {
    const int error = errno;
    ::close(fd);
    if (error == ENOENT) return types::Status::failure("no wpa_supplicant control socket at " + path);
    if (error == EACCES) return types::Status::failure("not allowed to use " + path + " (the service user needs the netdev group)");
    if (error == ECONNREFUSED) return types::Status::failure("nobody is listening on " + path);
    return types::Status::failure("cannot connect to " + path + ": " + errno_text(error));
  }

  boost::system::error_code ec;
  socket.assign(boost::asio::local::datagram_protocol(), fd, ec);
  if (ec) {
    ::close(fd);
    return types::Status::failure("cannot watch " + path + ": " + ec.message());
  }

  // A full receive queue at wpa_supplicant's end is an error now, not a stalled io thread.
  socket.non_blocking(true, ec);
  return types::Status::success();
}

void EasyConnectWpaSupplicant::_exchange(Socket& socket, const std::string& command, ReplyHandler handler) {
  struct Exchange {
    bool done = false;
    std::array<char, 4096> buffer{};
    ReplyHandler handler;
    std::shared_ptr<timers::Timer> timer;
  };

  auto exchange = std::make_shared<Exchange>();
  exchange->handler = std::move(handler);

  boost::system::error_code ec;
  socket.send(boost::asio::buffer(command), 0, ec);
  if (ec) {
    types::complete(_executor, std::move(exchange->handler), types::Result<std::string>::failure("cannot send " + command + ": " + ec.message()));
    return;
  }

  socket.async_receive(boost::asio::buffer(exchange->buffer), [exchange](const boost::system::error_code& ec, std::size_t length) {
    if (exchange->done) return;
    exchange->done = true;
    if (exchange->timer) exchange->timer->cancel();

    if (ec) return exchange->handler(types::Result<std::string>::failure(ec.message()));
    exchange->handler(types::Result<std::string>::success(std::string(exchange->buffer.data(), length)));
  });

  // The command's word only: what follows can be long, and is in the debug log anyway.
  const std::string word = command.substr(0, command.find(' '));
  exchange->timer = _timers->schedule(_options.reply_timeout, [weak = weak_from_this(), exchange, socket = &socket, word]() {
    if (exchange->done) return;
    exchange->done = true;
    if (auto self = weak.lock()) {
      boost::system::error_code ignored;
      socket->cancel(ignored);
    }
    exchange->handler(types::Result<std::string>::failure("wpa_supplicant did not answer " + word));
  });
}

void EasyConnectWpaSupplicant::start(types::Executor on, types::Handler<std::string> started, types::Handler<EasyConnectCredentials> received) {
  if (_active) stop();

  ++_generation;
  _active = true;
  _on = std::move(on);
  _started = std::move(started);
  _received = std::move(received);
  _configuring = false;
  _credentials = EasyConnectCredentials{};

  if (auto opened = _open(_commands); !opened) return _fail_start(opened.error);
  if (auto opened = _open(_events); !opened) return _fail_start(opened.error);

  const auto generation = _generation;
  auto alive = [weak = weak_from_this(), generation]() -> std::shared_ptr<EasyConnectWpaSupplicant> {
    auto self = weak.lock();
    if (!self || self->_generation != generation || !self->_active) return nullptr;
    return self;
  };

  _exchange(_events, "ATTACH", [this, alive](types::Result<std::string> attached) {
    auto self = alive();
    if (!self) return;
    if (!attached || wpa::reply_text(attached.value) != "OK")
      return _fail_start("wpa_supplicant would not send events (ATTACH: " + attached.error + attached.value + ")");
    _read_events();

    _exchange(_commands, "STATUS", [this, alive](types::Result<std::string> status) {
      auto self = alive();
      if (!self) return;
      if (!status) return _fail_start(status.error);

      const auto mac = wpa::status_mac(status.value);
      if (!mac) return _fail_start("wpa_supplicant's STATUS for " + _options.interface + " has no address");

      // Associated on 2.4 GHz: listen on that channel and list it first (docs/protocol.md).
      // An off-channel listen on a Raspberry Pi 5's Broadcom radio was never heard.
      unsigned listen_frequency = _options.listen_frequency;
      std::string channels = _options.channels;
      if (const auto freq = status_frequency(status.value); freq >= 2412 && freq <= 2472) {
        listen_frequency = freq;
        channels = "81/" + std::to_string((freq - 2407) / 5) + "," + _options.channels;
      }

      const std::string generate = "DPP_BOOTSTRAP_GEN type=qrcode chan=" + channels + " mac=" + *mac;
      _exchange(_commands, generate, [this, alive, listen_frequency](types::Result<std::string> generated) {
        auto self = alive();
        if (!self) return;
        if (!generated) return _fail_start(generated.error);
        if (wpa::reply_text(generated.value) == "UNKNOWN COMMAND") return _fail_start("this wpa_supplicant was built without DPP");
        if (wpa::is_refusal(generated.value)) return _fail_start("wpa_supplicant refused DPP_BOOTSTRAP_GEN");

        _bootstrap_id = wpa::reply_text(generated.value);
        _exchange(_commands, "DPP_BOOTSTRAP_GET_URI " + _bootstrap_id, [this, alive, listen_frequency](types::Result<std::string> got) {
          auto self = alive();
          if (!self) return;
          if (!got) return _fail_start(got.error);

          const auto uri = wpa::reply_text(got.value);
          if (uri.rfind("DPP:", 0) != 0) return _fail_start("wpa_supplicant gave no bootstrap URI");

          const std::string listen = "DPP_LISTEN " + std::to_string(listen_frequency) + " role=enrollee";
          _exchange(_commands, listen, [this, alive, uri, listen_frequency](types::Result<std::string> listening) {
            auto self = alive();
            if (!self) return;
            if (!listening) return _fail_start(listening.error);
            if (wpa::is_refusal(listening.value)) return _fail_start("wpa_supplicant refused DPP_LISTEN");

            _listening = true;
            _logger->info("Wi-Fi Easy Connect: listening on " + std::to_string(listen_frequency) + " MHz for " +
                          std::to_string(_options.listen_time.count() / 1000) + " s");

            const auto generation = _generation;
            _listen_timer = _timers->schedule(_options.listen_time, [weak = weak_from_this(), generation]() {
              auto self = weak.lock();
              if (!self || self->_generation != generation) return;
              self->_finish(types::Result<EasyConnectCredentials>::failure("no phone sent a network within " +
                                                                           std::to_string(self->_options.listen_time.count() / 1000) + " s"));
            });

            auto started = std::move(_started);
            types::complete(_on, std::move(started), types::Result<std::string>::success(uri));
          });
        });
      });
    });
  });
}

void EasyConnectWpaSupplicant::stop() {
  if (!_active) return;
  _logger->info("Wi-Fi Easy Connect: stopped");

  // Stopped while still being set up: the start is answered, so nobody waits for ever.
  auto started = std::move(_started);
  auto on = _on;
  _cleanup();
  if (started) types::complete(on, std::move(started), types::Result<std::string>::failure(easyconnect_unavailable, "stopped before it was listening"));
}

void EasyConnectWpaSupplicant::_read_events() {
  const auto generation = _generation;
  _events.async_receive(boost::asio::buffer(_event_buffer), [weak = weak_from_this(), generation](const boost::system::error_code& ec, std::size_t length) {
    auto self = weak.lock();
    if (!self || self->_generation != generation || !self->_active) return;

    if (ec) {
      self->_finish(types::Result<EasyConnectCredentials>::failure("lost wpa_supplicant's events: " + ec.message()));
      return;
    }

    self->_on_event(std::string(self->_event_buffer.data(), length));
    if (self->_generation == generation && self->_active) self->_read_events();
  });
}

void EasyConnectWpaSupplicant::_on_event(const std::string& message) {
  const auto event = wpa::parse_event(message);
  if (event.name.rfind("DPP-", 0) != 0) return;

  using Out = types::Result<EasyConnectCredentials>;

  if (event.name == "DPP-CONF-RECEIVED") {
    _configuring = true;
    _credentials = EasyConnectCredentials{};
    _logger->info("Wi-Fi Easy Connect: a configuration arrived");

    // The configuration's events follow at once; one that ends without a passphrase or
    // PSK is a network this cannot hand to NetworkManager.
    const auto generation = _generation;
    _settle_timer = _timers->schedule(_options.settle, [weak = weak_from_this(), generation]() {
      auto self = weak.lock();
      if (!self || self->_generation != generation) return;
      self->_finish(Out::failure("the configuration had no passphrase (AKM " + self->_credentials.akm +
                                 "); a DPP-only network cannot be joined through NetworkManager here"));
    });
    return;
  }

  if (event.name == "DPP-CONF-FAILED") return _finish(Out::failure("the phone's configuration failed (DPP-CONF-FAILED)"));

  if (event.name == "DPP-AUTH-SUCCESS") {
    _logger->info("Wi-Fi Easy Connect: the phone authenticated");
    return;
  }

  if (!_configuring) {
    _logger->debug("Wi-Fi Easy Connect: " + event.name + " " + event.argument);
    return;
  }

  if (event.name == "DPP-CONFOBJ-AKM") {
    _credentials.akm = event.argument;
    return;
  }

  if (event.name == "DPP-CONFOBJ-SSID") {
    const auto ssid = wpa::decode_ssid(event.argument);
    if (!ssid || ssid->empty()) return _finish(Out::failure("the configuration's SSID could not be read: " + event.argument));
    _credentials.ssid = *ssid;
    return;
  }

  if (event.name == "DPP-CONFOBJ-PASS" || event.name == "DPP-CONFOBJ-PSK") {
    if (_credentials.ssid.empty()) return _finish(Out::failure("the configuration had a passphrase and no SSID"));

    if (event.name == "DPP-CONFOBJ-PASS") {
      const auto passphrase = wpa::decode_hex_text(event.argument);
      if (!passphrase) return _finish(Out::failure("the configuration's passphrase could not be read"));
      _credentials.passphrase = *passphrase;
    } else {
      // A raw PSK, as 64 hex characters, which NetworkManager's psk takes as it is.
      if (event.argument.size() != 64 || !wpa::decode_hex_text(event.argument)) return _finish(Out::failure("the configuration's PSK could not be read"));
      _credentials.passphrase = event.argument;
    }

    return _finish(Out::success(_credentials));
  }
}

void EasyConnectWpaSupplicant::_fail_start(const std::string& why) {
  _logger->warn("Wi-Fi Easy Connect cannot start: " + why);
  auto started = std::move(_started);
  auto on = _on;
  _cleanup();
  types::complete(on, std::move(started), types::Result<std::string>::failure(easyconnect_unavailable, why));
}

void EasyConnectWpaSupplicant::_finish(types::Result<EasyConnectCredentials> result) {
  if (!_active) return;

  if (result) {
    _logger->info("Wi-Fi Easy Connect: received " + result.value.ssid + " (AKM " + result.value.akm + ")");
  } else {
    _logger->warn("Wi-Fi Easy Connect: " + result.error);
  }

  auto received = std::move(_received);
  auto on = _on;
  _cleanup();
  types::complete(on, std::move(received), std::move(result));
}

void EasyConnectWpaSupplicant::_cleanup() {
  ++_generation;
  _active = false;
  _configuring = false;

  if (_listen_timer) _listen_timer->cancel();
  _listen_timer.reset();
  if (_settle_timer) _settle_timer->cancel();
  _settle_timer.reset();

  // Fire and forget: a datagram is in wpa_supplicant's queue once sent, and its replies
  // go to a socket about to close, which it shrugs off.
  boost::system::error_code ignored;
  if (_commands.is_open()) {
    if (_listening) _commands.send(boost::asio::buffer(std::string("DPP_STOP_LISTEN")), 0, ignored);
    if (!_bootstrap_id.empty()) _commands.send(boost::asio::buffer("DPP_BOOTSTRAP_REMOVE " + _bootstrap_id), 0, ignored);
    _commands.close(ignored);
  }
  if (_events.is_open()) {
    _events.send(boost::asio::buffer(std::string("DETACH")), 0, ignored);
    _events.close(ignored);
  }

  _listening = false;
  _bootstrap_id.clear();
  _started = nullptr;
  _received = nullptr;
}

}  // namespace rpiledconfig::wifi

#endif
