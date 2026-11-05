//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#if RPILEDCONFIG_HAVE_SD_BUS

#include <systemd/sd-bus.h>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "loggers/logger.h"
#include "types/status.h"

namespace rpiledconfig::dbus {

struct MessageUnref {
  void operator()(sd_bus_message* message) const { sd_bus_message_unref(message); }
};
using Message = std::unique_ptr<sd_bus_message, MessageUnref>;

struct SlotUnref {
  void operator()(sd_bus_slot* slot) const { sd_bus_slot_unref(slot); }
};
using Slot = std::unique_ptr<sd_bus_slot, SlotUnref>;

// A method call that failed: the error the peer (or sd-bus, or the bus) gave.
struct CallError {
  std::string name;
  std::string message;

  // The message when there is one, the name when not.
  std::string describe() const { return message.empty() ? name : message; }
};

// The answer to a method call: the reply, positioned at its first argument, or the error
// and no reply. Called once, on the loop's executor.
using ReplyHandler = std::function<void(sd_bus_message* reply, const CallError* error)>;

// The one connection to a bus, and the only thing in the daemon that touches it
// sd-bus is single-threaded and must be pumped: the
// loop watches sd_bus_get_fd for what sd_bus_get_events asks, honours sd_bus_get_timeout
// with a timer, and calls sd_bus_process until it has nothing left. All of that runs on
// the executor it was given, and so does everything that uses bus(): the drivers are
// made on the same executor, so a vtable handler, a reply and a driver's own calls never
// meet on two threads.
class SdBusLoop : public std::enable_shared_from_this<SdBusLoop> {
 public:
  static constexpr std::chrono::milliseconds default_timeout{25000};

  // An empty address is the system bus; anything else is a D-Bus address
  // (unix:path=/tmp/x), which is how the tests point it at a private bus.
  SdBusLoop(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::string address = {});
  ~SdBusLoop();

  SdBusLoop(const SdBusLoop&) = delete;
  SdBusLoop& operator=(const SdBusLoop&) = delete;

  // Connects and starts pumping. On the executor.
  types::Status open();

  // Flushes what is queued and closes the connection; every slot still held goes dead.
  // On the executor.
  void close();

  bool is_open() const { return _bus != nullptr; }

  // The raw connection, for exporting objects and emitting signals. On the executor only.
  sd_bus* bus() const { return _bus; }

  const types::Executor& executor() const { return _executor; }
  std::string describe() const { return _address.empty() ? "the system bus" : _address; }

  // A method call to fill in and hand to call(). Null when the bus is closed.
  Message method_call(const char* destination, const char* path, const char* interface, const char* member);

  // Sends a method call; the handler gets the reply on the executor. Never answers inline:
  // a call that cannot even be sent is answered with a posted error.
  void call(Message message, ReplyHandler handler, std::chrono::milliseconds timeout = default_timeout);

  // org.freedesktop.DBus.Properties.Get; the reply is positioned at the variant.
  void get_property(const char* destination, const char* path, const char* interface, const char* property, ReplyHandler handler);

  // org.freedesktop.DBus.Properties.GetAll; the reply is positioned at the a{sv}.
  void get_all_properties(const char* destination, const char* path, const char* interface, ReplyHandler handler);

  // Asks for a pump soon. Whatever is sent from outside the loop's own processing (a
  // signal emitted, a reply sent later) may sit in sd-bus's write queue until the loop
  // looks at the events again; this is how it is told to.
  void kick();

 private:
  struct Pending {
    ReplyHandler handler;
  };

  static int _on_reply(sd_bus_message* reply, void* userdata, sd_bus_error* error);
  static void _on_pending_destroyed(void* userdata);

  void _pump();
  void _arm();
  void _fail(const std::string& why);
  void _post_error(ReplyHandler handler, std::string name, std::string message);

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::string _address;

  sd_bus* _bus = nullptr;
  std::unique_ptr<boost::asio::posix::stream_descriptor> _fd;
  boost::asio::steady_timer _timer;
  bool _read_waiting = false;
  bool _write_waiting = false;
  bool _pump_posted = false;
  bool _processing = false;
};

}  // namespace rpiledconfig::dbus

#endif
