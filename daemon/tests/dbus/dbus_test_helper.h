//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <systemd/sd-bus.h>

#include <atomic>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "helpers/io_thread_helper.h"
#include "types/status.h"

// The D-Bus tests run against a private dbus-daemon whose address is in
// RPILEDCONFIG_TEST_DBUS_ADDRESS (scripts/linux-tests.sh starts one), never the system
// bus. Without it they skip, as the Redis tests in athenasip skip without a server.
inline std::string test_bus_address() {
  const char* address = std::getenv("RPILEDCONFIG_TEST_DBUS_ADDRESS");
  return address ? address : "";
}

#define REQUIRE_TEST_BUS()                                                                       \
  do {                                                                                           \
    if (test_bus_address().empty()) GTEST_SKIP() << "RPILEDCONFIG_TEST_DBUS_ADDRESS is not set"; \
  } while (0)

// One fake system service (BlueZ, NetworkManager): its own connection to the private bus,
// owning its well-known name, pumped by its own thread with plain sd_bus_process and
// sd_bus_wait. Nothing here uses the daemon's SdBusLoop, so the two cannot share a
// mistake. Everything that touches the connection or the service's state runs on that
// thread; the test reaches it through run().
class MockService {
 public:
  explicit MockService(std::string address) : _address(std::move(address)) {}
  virtual ~MockService() { stop(); }

  MockService(const MockService&) = delete;
  MockService& operator=(const MockService&) = delete;

  // Connects, exports (setup(), on this thread before the pump starts), takes the name.
  bool start(const char* name) {
    if (sd_bus_new(&_bus) < 0) return false;
    if (sd_bus_set_address(_bus, _address.c_str()) < 0 || sd_bus_set_bus_client(_bus, 1) < 0 || sd_bus_start(_bus) < 0) return false;
    if (setup() < 0) return false;
    if (sd_bus_request_name(_bus, name, 0) < 0) return false;

    _stop = false;
    _thread = std::thread([this]() { _pump(); });
    return true;
  }

  // Lets the name go and disconnects: to the daemon, the service has gone away.
  void stop() {
    if (_thread.joinable()) {
      _stop = true;
      _thread.join();
    }
    if (_bus) {
      teardown();
      sd_bus_flush_close_unref(_bus);
      _bus = nullptr;
    }
  }

  // Runs fn on the service's thread and gives back what it returned.
  template <typename F>
  auto run(F fn) -> decltype(fn()) {
    using R = decltype(fn());
    if (std::this_thread::get_id() == _thread.get_id()) return fn();

    auto task = std::make_shared<std::packaged_task<R()>>(std::move(fn));
    auto future = task->get_future();
    {
      std::lock_guard<std::mutex> lock(_mutex);
      _queue.emplace_back([task]() { (*task)(); });
    }
    return future.get();
  }

  // Runs fn on the service's thread after a delay, as a real daemon finishing something
  // later would. From the service's thread.
  void later(std::chrono::milliseconds delay, std::function<void()> fn) { _timers.emplace_back(std::chrono::steady_clock::now() + delay, std::move(fn)); }

 protected:
  virtual int setup() = 0;
  virtual void teardown() {}

  sd_bus* bus() const { return _bus; }

 private:
  void _pump() {
    while (!_stop) {
      while (sd_bus_process(_bus, nullptr) > 0) {
      }

      std::deque<std::function<void()>> queue;
      {
        std::lock_guard<std::mutex> lock(_mutex);
        queue.swap(_queue);
      }
      for (auto& fn : queue) fn();

      const auto now = std::chrono::steady_clock::now();
      std::vector<std::function<void()>> due;
      for (auto it = _timers.begin(); it != _timers.end();) {
        if (it->first <= now) {
          due.push_back(std::move(it->second));
          it = _timers.erase(it);
        } else {
          ++it;
        }
      }
      for (auto& fn : due) fn();

      sd_bus_wait(_bus, 2000);
    }
  }

  std::string _address;
  sd_bus* _bus = nullptr;
  std::thread _thread;
  std::atomic<bool> _stop{false};
  std::mutex _mutex;
  std::deque<std::function<void()>> _queue;
  std::vector<std::pair<std::chrono::steady_clock::time_point, std::function<void()>>> _timers;
};
