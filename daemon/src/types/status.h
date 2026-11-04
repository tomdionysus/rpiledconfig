//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rpiledconfig::types {

// Bytes on the wire and in the derivations: everything the protocol handles is one of these.
using Bytes = std::vector<std::uint8_t>;

// The executor a caller hands a backend so that its completion handler runs back where
// the caller lives. Setup mode hands in its strand; a backend never decides for itself
// which thread application code runs on.
using Executor = boost::asio::any_io_executor;

// What an operation that returns nothing but can fail reports back. The code is for the
// protocol's error table (wifi_unavailable, wifi_failed): a backend that knows which one
// it is says so, and the request dispatcher passes it on; empty means "the caller
// chooses". The error is for a person.
struct Status {
  bool ok = false;
  std::string error;
  std::string code;

  static Status success() { return Status{true, {}, {}}; }
  static Status failure(std::string reason) { return Status{false, std::move(reason), {}}; }
  static Status failure(std::string code, std::string reason) { return Status{false, std::move(reason), std::move(code)}; }

  explicit operator bool() const { return ok; }
};

// What an operation that returns something reports back. A scan that finds nothing is a
// success carrying an empty list, not a failure: "no networks" and "NetworkManager is
// down" are different answers, and the app has to tell them apart.
template <typename T>
struct Result {
  bool ok = false;
  std::string error;
  std::string code;
  T value{};

  static Result success(T value) { return Result{true, {}, {}, std::move(value)}; }
  static Result failure(std::string reason) { return Result{false, std::move(reason), {}, T{}}; }
  static Result failure(std::string code, std::string reason) { return Result{false, std::move(reason), std::move(code), T{}}; }

  explicit operator bool() const { return ok; }
};

template <typename T>
using Handler = std::function<void(Result<T>)>;

using StatusHandler = std::function<void(Status)>;

// How a backend answers. The handler is posted to the caller's executor and never called
// inline, so a caller is never re-entered from inside its own call. The fakes could
// answer at once and the D-Bus drivers cannot, and code written against the first has to
// work against the second.
template <typename H, typename V>
void complete(Executor on, H handler, V value) {
  if (!handler) return;

  boost::asio::post(std::move(on), [handler = std::move(handler), value = std::move(value)]() mutable { handler(std::move(value)); });
}

}  // namespace rpiledconfig::types
