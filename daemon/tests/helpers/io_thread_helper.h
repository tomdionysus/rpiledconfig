//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <utility>

#include "types/status.h"

// For tests whose daemon side does real I/O (a bus, a socket) and so cannot be drained by
// hand: a thread of its own to run on, a way to wait for an answer, and a way to wait for
// a condition. Nothing here sleeps for a fixed time and hopes.

// Polls until the predicate holds or the time is up. The predicate asks a thread for its
// state (DaemonThread::run, a fake's own accessors).
inline bool eventually(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  const auto until = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < until) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

// The daemon's side: one io thread and a strand on it, as main.cpp has. What is under
// test lives on the strand; the test reaches it through run().
class DaemonThread {
 public:
  DaemonThread() : _guard(boost::asio::make_work_guard(_io)), _strand(boost::asio::make_strand(_io)) {
    _thread = std::thread([this]() { _io.run(); });
  }

  ~DaemonThread() {
    _guard.reset();
    _io.stop();
    if (_thread.joinable()) _thread.join();
  }

  DaemonThread(const DaemonThread&) = delete;
  DaemonThread& operator=(const DaemonThread&) = delete;

  const rpiledconfig::types::Executor& executor() const { return _strand; }

  template <typename F>
  auto run(F fn) -> decltype(fn()) {
    using R = decltype(fn());
    auto task = std::make_shared<std::packaged_task<R()>>(std::move(fn));
    auto future = task->get_future();
    boost::asio::post(_strand, [task]() { (*task)(); });
    return future.get();
  }

 private:
  boost::asio::io_context _io;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
  rpiledconfig::types::Executor _strand;
  std::thread _thread;
};

// A completion handler's value, waited for on the test's thread.
template <typename T>
class Awaited {
 public:
  std::function<void(T)> handler() {
    return [promise = _promise](T value) { promise->set_value(std::move(value)); };
  }

  // Nothing (a default T, and false) if the handler was not called within the timeout.
  bool wait(T& out, std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    auto future = _promise->get_future();
    if (future.wait_for(timeout) != std::future_status::ready) return false;
    out = future.get();
    return true;
  }

 private:
  std::shared_ptr<std::promise<T>> _promise = std::make_shared<std::promise<T>>();
};
