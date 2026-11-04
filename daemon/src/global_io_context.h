//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <utility>

namespace rpiledconfig::detail {

// The io_context for the whole process, run by exactly one thread. The LED modulator, the
// BLE service, the WiFi backend and every timer live on it; objects that are reached from
// more than one place own a strand on it rather than a lock.
//
// One thread is enough for a daemon that blinks one LED and talks to one phone, and it
// makes "which thread am I on" a question with one answer, which is worth more than
// parallelism nothing needs.
inline boost::asio::io_context& get_global_io_context() {
  static boost::asio::io_context io_context;

  // run() would return the moment there was nothing queued, which at startup is always.
  static auto work_guard = boost::asio::make_work_guard(io_context);

  static struct RunThread {
    RunThread() {
      t = std::thread([&]() { io_context.run(); });
    }
    ~RunThread() {
      work_guard.reset();

      // Releasing the guard is not enough: a pending timer is work in its own right, and
      // a recheck a minute out would hold run() open for a minute. By the time this runs
      // the process is leaving, so abandon them rather than wait.
      io_context.stop();

      if (t.joinable()) {
        t.join();
      }
    }
    std::thread t;
  } run_thread;

  return io_context;
}

// Whether the caller is the io thread. Anything that waits for the io thread must not be
// called from it, and this is how the synchronous teardown paths tell.
inline bool on_io_thread() { return get_global_io_context().get_executor().running_in_this_thread(); }

// Runs fn on the executor and waits for it, for the synchronous teardown paths (stop)
// that are called from main or from a test. From the io thread itself it runs inline,
// because waiting there would wait for ever.
template <typename Executor>
void run_and_wait(const Executor& executor, std::function<void()> fn, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  if (on_io_thread()) {
    fn();
    return;
  }

  auto promise = std::make_shared<std::promise<void>>();
  auto future = promise->get_future();

  boost::asio::post(executor, [promise, fn = std::move(fn)]() {
    fn();
    promise->set_value();
  });

  future.wait_for(timeout);
}

}  // namespace rpiledconfig::detail
