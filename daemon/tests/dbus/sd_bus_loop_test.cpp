//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "dbus/sd_bus_loop.h"

#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include "dbus/sd_bus_helpers.h"
#include "dbus_test_helper.h"
#include "mocks/logger_mock.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

// A service with an echo, a method that never answers, and a signal on demand.
class EchoService : public MockService {
 public:
  using MockService::MockService;
  ~EchoService() override { stop(); }

  void emit_ping() {
    run([this]() { sd_bus_emit_signal(bus(), "/test", "com.tomcully.Test", "Ping", "s", "hello"); });
  }

  int unanswered() {
    return run([this]() { return static_cast<int>(_unanswered.size()); });
  }

 protected:
  int setup() override { return sd_bus_add_object_vtable(bus(), &_slot, "/test", "com.tomcully.Test", vtable, this); }

  void teardown() override {
    for (auto* m : _unanswered) sd_bus_message_unref(m);
    _unanswered.clear();
    sd_bus_slot_unref(_slot);
  }

 private:
  static int echo(sd_bus_message* m, void*, sd_bus_error*) {
    const char* text = nullptr;
    const int r = sd_bus_message_read(m, "s", &text);
    if (r < 0) return r;
    return sd_bus_reply_method_return(m, "s", text);
  }

  static int never(sd_bus_message* m, void* userdata, sd_bus_error*) {
    static_cast<EchoService*>(userdata)->_unanswered.push_back(sd_bus_message_ref(m));
    return 1;
  }

  static int refuse(sd_bus_message*, void*, sd_bus_error* error) { return sd_bus_error_set(error, "com.tomcully.Test.Error.No", "no, and here is why"); }

  static constexpr sd_bus_vtable vtable[] = {
      SD_BUS_VTABLE_START(0),
      SD_BUS_METHOD("Echo", "s", "s", echo, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("Never", "", "", never, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("Refuse", "", "", refuse, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_SIGNAL("Ping", "s", 0),
      SD_BUS_VTABLE_END,
  };

  sd_bus_slot* _slot = nullptr;
  std::vector<sd_bus_message*> _unanswered;
};

struct Answer {
  bool ok = false;
  std::string text;
  std::string error_name;
  bool on_executor = false;
};

struct LoopFixture : public ::testing::Test {
  void SetUp() override {
    if (test_bus_address().empty()) return;
    service = std::make_unique<EchoService>(test_bus_address());
    ASSERT_TRUE(service->start("com.tomcully.Test"));
    loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor(), test_bus_address());
    ASSERT_TRUE(daemon.run([this]() { return loop->open(); }).ok) << logger->text();
  }

  void TearDown() override {
    if (loop) daemon.run([this]() { loop->close(); });
    loop.reset();
    service.reset();
  }

  Answer call(const char* member, std::chrono::milliseconds timeout = dbus::SdBusLoop::default_timeout, const char* destination = "com.tomcully.Test") {
    Awaited<Answer> answer;
    auto handler = answer.handler();
    daemon.run([&]() {
      auto message = loop->method_call(destination, "/test", "com.tomcully.Test", member);
      if (std::string(member) == "Echo") sd_bus_message_append(message.get(), "s", "over the bus");

      loop->call(
          std::move(message),
          [this, handler](sd_bus_message* reply, const dbus::CallError* error) {
            Answer a;
            a.on_executor = daemon.executor().target<boost::asio::strand<boost::asio::io_context::executor_type>>()->running_in_this_thread();
            if (error) {
              a.error_name = error->name;
            } else {
              a.ok = true;
              const char* text = nullptr;
              if (sd_bus_message_read(reply, "s", &text) > 0) a.text = text;
            }
            handler(a);
          },
          timeout);
    });

    Answer out;
    EXPECT_TRUE(answer.wait(out)) << "no answer to " << member;
    return out;
  }

  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  DaemonThread daemon;
  std::unique_ptr<EchoService> service;
  std::shared_ptr<dbus::SdBusLoop> loop;
};

}  // namespace

TEST_F(LoopFixture, AMethodCallIsAnsweredOnTheLoopsExecutor) {
  REQUIRE_TEST_BUS();

  const auto answer = call("Echo");
  EXPECT_TRUE(answer.ok) << answer.error_name;
  EXPECT_EQ(answer.text, "over the bus");
  EXPECT_TRUE(answer.on_executor);
}

TEST_F(LoopFixture, ManyCallsInFlightAreAllAnswered) {
  REQUIRE_TEST_BUS();

  constexpr int count = 200;
  std::atomic<int> answered{0};
  daemon.run([&]() {
    for (int i = 0; i < count; ++i) {
      auto message = loop->method_call("com.tomcully.Test", "/test", "com.tomcully.Test", "Echo");
      sd_bus_message_append(message.get(), "s", std::to_string(i).c_str());
      loop->call(std::move(message), [&answered](sd_bus_message*, const dbus::CallError* error) {
        if (!error) ++answered;
      });
    }
  });

  EXPECT_TRUE(eventually([&]() { return answered.load() == count; })) << answered.load();
}

TEST_F(LoopFixture, AnErrorReplyCarriesTheNameAndMessage) {
  REQUIRE_TEST_BUS();

  const auto answer = call("Refuse");
  EXPECT_FALSE(answer.ok);
  EXPECT_EQ(answer.error_name, "com.tomcully.Test.Error.No");
}

TEST_F(LoopFixture, ACallToANameNobodyOwnsIsUnavailable) {
  REQUIRE_TEST_BUS();

  const auto answer = call("Echo", dbus::SdBusLoop::default_timeout, "com.tomcully.Nobody");
  EXPECT_FALSE(answer.ok);
  EXPECT_TRUE(dbus::is_unavailable(dbus::CallError{answer.error_name, ""})) << answer.error_name;
}

TEST_F(LoopFixture, TheTimeoutIsHonouredWithoutAnyTrafficToWakeTheLoop) {
  REQUIRE_TEST_BUS();

  const auto started = std::chrono::steady_clock::now();
  const auto answer = call("Never", 300ms);
  const auto took = std::chrono::steady_clock::now() - started;

  EXPECT_FALSE(answer.ok);
  EXPECT_TRUE(dbus::is_unavailable(dbus::CallError{answer.error_name, ""})) << answer.error_name;
  EXPECT_GE(took, 250ms);
  EXPECT_LT(took, 3s);
  EXPECT_EQ(service->unanswered(), 1);
}

TEST_F(LoopFixture, SignalsAreDispatchedThroughAMatch) {
  REQUIRE_TEST_BUS();

  std::atomic<int> pings{0};
  dbus::Slot slot;
  std::atomic<bool> installed{false};

  struct Context {
    std::atomic<int>* pings;
    std::atomic<bool>* installed;
  } context{&pings, &installed};

  daemon.run([&]() {
    sd_bus_slot* raw = nullptr;
    sd_bus_match_signal_async(
        loop->bus(), &raw, nullptr, "/test", "com.tomcully.Test", "Ping",
        [](sd_bus_message*, void* userdata, sd_bus_error*) {
          ++*static_cast<Context*>(userdata)->pings;
          return 0;
        },
        [](sd_bus_message*, void* userdata, sd_bus_error*) {
          *static_cast<Context*>(userdata)->installed = true;
          return 0;
        },
        &context);
    slot.reset(raw);
    loop->kick();
  });

  ASSERT_TRUE(eventually([&]() { return installed.load(); }));
  service->emit_ping();
  service->emit_ping();
  EXPECT_TRUE(eventually([&]() { return pings.load() == 2; })) << pings.load();

  daemon.run([&]() { slot.reset(); });
}

TEST_F(LoopFixture, AfterCloseACallIsAnsweredDisconnected) {
  REQUIRE_TEST_BUS();

  daemon.run([this]() { loop->close(); });
  EXPECT_FALSE(daemon.run([this]() { return loop->is_open(); }));

  const auto answer = call("Echo");
  EXPECT_FALSE(answer.ok);
  EXPECT_EQ(answer.error_name, SD_BUS_ERROR_DISCONNECTED);
}

TEST(SdBusLoopTest, AnAddressNobodyListensOnIsRefused) {
  REQUIRE_TEST_BUS();

  DaemonThread daemon;
  auto logger = std::make_shared<MockLogger>();
  auto loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor(), "unix:path=/nonexistent/rpiledconfig-test-bus");

  const auto opened = daemon.run([&]() { return loop->open(); });
  EXPECT_FALSE(opened.ok);
  EXPECT_NE(opened.error.find("cannot connect"), std::string::npos) << opened.error;
}
