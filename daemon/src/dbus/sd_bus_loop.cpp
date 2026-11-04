//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "sd_bus_loop.h"

#if RPILEDCONFIG_HAVE_SD_BUS

#include <poll.h>
#include <time.h>

#include <boost/asio/post.hpp>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <utility>

namespace rpiledconfig::dbus {

namespace {

std::string errno_text(int r) { return std::strerror(r < 0 ? -r : r); }

// sd_bus_get_timeout answers in CLOCK_MONOTONIC microseconds, absolute.
std::uint64_t monotonic_usec() {
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000u + static_cast<std::uint64_t>(ts.tv_nsec) / 1000u;
}

// One pump processes at most this many messages before letting the rest of the io thread
// run, so a burst of signals cannot starve the LED's timers.
constexpr int max_messages_per_pump = 64;

}  // namespace

SdBusLoop::SdBusLoop(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::string address)
    : _logger(std::move(logger)), _executor(std::move(executor)), _address(std::move(address)), _timer(_executor) {}

SdBusLoop::~SdBusLoop() {
  // Normally closed on the executor already. If not, the process is leaving and nothing
  // else runs on the bus any more.
  if (_fd) {
    boost::system::error_code ignored;
    _fd->cancel(ignored);
    _fd->release();
    _fd.reset();
  }
  if (_bus) sd_bus_flush_close_unref(_bus);
  _bus = nullptr;
}

types::Status SdBusLoop::open() {
  if (_bus) return types::Status::success();

  sd_bus* bus = nullptr;
  int r = 0;

  if (_address.empty()) {
    r = sd_bus_open_system(&bus);
  } else {
    r = sd_bus_new(&bus);
    if (r >= 0) r = sd_bus_set_address(bus, _address.c_str());
    if (r >= 0) r = sd_bus_set_bus_client(bus, 1);
    if (r >= 0) r = sd_bus_start(bus);
  }

  if (r < 0) {
    if (bus) sd_bus_unref(bus);
    return types::Status::failure("cannot connect to " + describe() + ": " + errno_text(r));
  }

  sd_bus_set_description(bus, "rpiledconfig");

  const int fd = sd_bus_get_fd(bus);
  if (fd < 0) {
    sd_bus_flush_close_unref(bus);
    return types::Status::failure("no descriptor for " + describe() + ": " + errno_text(fd));
  }

  try {
    _fd = std::make_unique<boost::asio::posix::stream_descriptor>(_executor, fd);
  } catch (const boost::system::system_error& e) {
    sd_bus_flush_close_unref(bus);
    return types::Status::failure("cannot watch " + describe() + ": " + e.what());
  }

  _bus = bus;
  _read_waiting = false;
  _write_waiting = false;
  _pump_posted = false;

  _logger->info("Connected to " + describe());
  kick();
  return types::Status::success();
}

void SdBusLoop::close() {
  if (!_bus) return;

  // From inside sd_bus_process (a handler deciding to close), freeing the bus would pull
  // it out from under the call that is running; later, then.
  if (_processing) {
    boost::asio::post(_executor, [weak = weak_from_this()]() {
      if (auto self = weak.lock()) self->close();
    });
    return;
  }

  _timer.cancel();

  // The descriptor is sd-bus's, not ours: released, never closed by asio.
  if (_fd) {
    boost::system::error_code ignored;
    _fd->cancel(ignored);
    _fd->release();
    _fd.reset();
  }

  sd_bus_flush_close_unref(_bus);
  _bus = nullptr;
  _logger->info("Disconnected from " + describe());
}

Message SdBusLoop::method_call(const char* destination, const char* path, const char* interface, const char* member) {
  if (!_bus) return nullptr;

  sd_bus_message* message = nullptr;
  if (sd_bus_message_new_method_call(_bus, &message, destination, path, interface, member) < 0) return nullptr;
  return Message(message);
}

void SdBusLoop::call(Message message, ReplyHandler handler, std::chrono::milliseconds timeout) {
  if (!_bus) return _post_error(std::move(handler), SD_BUS_ERROR_DISCONNECTED, "not connected to " + describe());
  if (!message) return _post_error(std::move(handler), SD_BUS_ERROR_NO_MEMORY, "cannot build the method call");

  auto* pending = new Pending{std::move(handler)};
  sd_bus_slot* slot = nullptr;

  const auto usec = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
  const int r = sd_bus_call_async(_bus, &slot, message.get(), &SdBusLoop::_on_reply, pending, usec);
  if (r < 0) {
    auto failed = std::move(pending->handler);
    delete pending;
    return _post_error(std::move(failed), SD_BUS_ERROR_DISCONNECTED, "cannot send to " + describe() + ": " + errno_text(r));
  }

  // The slot frees the handler whichever way it ends: answered, timed out, or the bus
  // closing under it. Floating, so it lives as long as the call and no longer.
  sd_bus_slot_set_destroy_callback(slot, &SdBusLoop::_on_pending_destroyed);
  sd_bus_slot_set_floating(slot, 1);
  sd_bus_slot_unref(slot);

  kick();
}

void SdBusLoop::get_property(const char* destination, const char* path, const char* interface, const char* property, ReplyHandler handler) {
  auto message = method_call(destination, path, "org.freedesktop.DBus.Properties", "Get");
  if (message && sd_bus_message_append(message.get(), "ss", interface, property) < 0) message.reset();

  call(std::move(message), [handler = std::move(handler)](sd_bus_message* reply, const CallError* error) {
    if (error) return handler(nullptr, error);
    handler(reply, nullptr);
  });
}

void SdBusLoop::get_all_properties(const char* destination, const char* path, const char* interface, ReplyHandler handler) {
  auto message = method_call(destination, path, "org.freedesktop.DBus.Properties", "GetAll");
  if (message && sd_bus_message_append(message.get(), "s", interface) < 0) message.reset();

  call(std::move(message), std::move(handler));
}

void SdBusLoop::kick() {
  if (!_bus || _pump_posted) return;
  _pump_posted = true;

  boost::asio::post(_executor, [weak = weak_from_this()]() {
    if (auto self = weak.lock()) self->_pump();
  });
}

int SdBusLoop::_on_reply(sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* pending = static_cast<Pending*>(userdata);
  if (!pending || !pending->handler) return 0;

  // Taken out first: the slot (and with it pending) may be freed by whatever the handler
  // does next.
  auto handler = std::move(pending->handler);

  if (sd_bus_message_is_method_error(reply, nullptr)) {
    const sd_bus_error* e = sd_bus_message_get_error(reply);
    CallError error{e && e->name ? e->name : "", e && e->message ? e->message : ""};
    handler(nullptr, &error);
  } else {
    handler(reply, nullptr);
  }

  return 0;
}

void SdBusLoop::_on_pending_destroyed(void* userdata) { delete static_cast<Pending*>(userdata); }

void SdBusLoop::_pump() {
  _pump_posted = false;
  if (!_bus) return;

  for (int i = 0; i < max_messages_per_pump; ++i) {
    _processing = true;
    const int r = sd_bus_process(_bus, nullptr);
    _processing = false;

    if (r < 0) return _fail("processing " + describe() + " failed: " + errno_text(r));
    if (!_bus) return;
    if (r == 0) return _arm();
  }

  // More to do: after whatever else is waiting on the io thread.
  kick();
  _arm();
}

void SdBusLoop::_arm() {
  if (!_bus) return;

  const int events = sd_bus_get_events(_bus);
  if (events < 0) return _fail("cannot ask " + describe() + " what it waits for: " + errno_text(events));

  // sd_bus_process has read until the socket would block, so a later arrival is a new
  // readiness edge for the reactor; the wait is armed before the io thread looks again.
  if ((events & POLLIN) && !_read_waiting) {
    _read_waiting = true;
    _fd->async_wait(boost::asio::posix::stream_descriptor::wait_read, [weak = weak_from_this()](const boost::system::error_code& ec) {
      auto self = weak.lock();
      if (!self) return;
      self->_read_waiting = false;
      if (ec) return;
      self->_pump();
    });
  }

  if ((events & POLLOUT) && !_write_waiting) {
    _write_waiting = true;
    _fd->async_wait(boost::asio::posix::stream_descriptor::wait_write, [weak = weak_from_this()](const boost::system::error_code& ec) {
      auto self = weak.lock();
      if (!self) return;
      self->_write_waiting = false;
      if (ec) return;
      self->_pump();
    });
  }

  std::uint64_t until = 0;
  if (sd_bus_get_timeout(_bus, &until) < 0 || until == UINT64_MAX) {
    _timer.cancel();
    return;
  }

  const std::uint64_t now = monotonic_usec();
  const auto delay = std::chrono::microseconds(until > now ? until - now : 0);

  // expires_after cancels the wait already set, whose handler sees operation_aborted.
  _timer.expires_after(delay);
  _timer.async_wait([weak = weak_from_this()](const boost::system::error_code& ec) {
    if (ec) return;
    if (auto self = weak.lock()) self->_pump();
  });
}

void SdBusLoop::_fail(const std::string& why) {
  // A connection the bus has dropped (dbus restarting, say) does not come back on its own.
  // Every call after this is answered "not connected", which the drivers report as their
  // backend being unavailable; systemd restarting the daemon is what brings it back.
  _logger->error(why + "; closing the connection");
  close();
}

void SdBusLoop::_post_error(ReplyHandler handler, std::string name, std::string message) {
  if (!handler) return;

  boost::asio::post(_executor, [handler = std::move(handler), name = std::move(name), message = std::move(message)]() {
    CallError error{name, message};
    handler(nullptr, &error);
  });
}

}  // namespace rpiledconfig::dbus

#endif
