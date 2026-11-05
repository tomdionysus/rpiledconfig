//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
// The wpa_supplicant enrollee against a fake wpa_supplicant: a datagram socket in a
// temporary directory that answers the control commands and sends the events as
// wpa_supplicant 2.10 does (ctrl_iface_unix.c replies with sendto to the sender's
// address; an ATTACHed sender gets "<level>EVENT ..." messages). No radio, no bus.

#include "wifi/easy_connect_wpa_supplicant.h"

#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "helpers/io_thread_helper.h"
#include "helpers/temp_dir_helper.h"
#include "mocks/logger_mock.h"
#include "protocol/messages.h"
#include "timers/timer_source.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

class FakeWpaSupplicant {
 public:
  explicit FakeWpaSupplicant(const std::filesystem::path& path) : _path(path) {
    _fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
      _thread = std::thread([this]() { _serve(); });
    }
  }

  ~FakeWpaSupplicant() {
    _stop = true;
    if (_thread.joinable()) _thread.join();
    ::close(_fd);
    ::unlink(_path.c_str());
  }

  void without_dpp() { _without_dpp = true; }
  void refuse_listen() { _refuse_listen = true; }
  void silent_on(const std::string& word) {
    std::lock_guard<std::mutex> lock(_mutex);
    _silent_on = word;
  }

  std::vector<std::string> commands() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _commands;
  }

  bool received(const std::string& command) {
    for (const auto& c : commands()) {
      if (c == command) return true;
    }
    return false;
  }

  bool attached() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _attached_length > 0;
  }

  // As wpa_msg sends to a monitor: the level, then the event.
  void emit(const std::string& event) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_attached_length == 0) return;
    const std::string message = "<3>" + event;
    ::sendto(_fd, message.data(), message.size(), 0, reinterpret_cast<sockaddr*>(&_attached), _attached_length);
  }

 private:
  void _serve() {
    while (!_stop) {
      pollfd p{_fd, POLLIN, 0};
      if (::poll(&p, 1, 20) <= 0) continue;

      char buffer[4096];
      sockaddr_un from{};
      socklen_t from_length = sizeof(from);
      const auto length = ::recvfrom(_fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
      if (length < 0) continue;

      const std::string command(buffer, static_cast<std::size_t>(length));
      const std::string reply = _answer(command, from, from_length);
      if (reply.empty()) continue;
      ::sendto(_fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from), from_length);
    }
  }

  std::string _answer(const std::string& command, const sockaddr_un& from, socklen_t from_length) {
    std::lock_guard<std::mutex> lock(_mutex);
    _commands.push_back(command);

    const std::string word = command.substr(0, command.find(' '));
    if (!_silent_on.empty() && word == _silent_on) return "";

    if (command == "ATTACH") {
      _attached = from;
      _attached_length = from_length;
      return "OK\n";
    }
    if (command == "DETACH") {
      _attached_length = 0;
      return "OK\n";
    }
    if (command == "STATUS") return "bssid=00:00:00:00:00:00\nwpa_state=DISCONNECTED\np2p_device_address=8a:a2:9e:23:c4:b4\naddress=88:a2:9e:23:c4:b4\n";

    if (word.rfind("DPP_", 0) == 0 && _without_dpp) return "UNKNOWN COMMAND\n";
    if (word == "DPP_BOOTSTRAP_GEN") return "1";
    if (word == "DPP_BOOTSTRAP_GET_URI")
      return command == "DPP_BOOTSTRAP_GET_URI 1" ? "DPP:C:81/1,81/6,81/11;M:88a29e23c4b4;V:2;K:MDkwEwYHKoZIzj0CAQ;;" : "FAIL\n";
    if (word == "DPP_LISTEN") return _refuse_listen ? "FAIL\n" : "OK\n";
    if (word == "DPP_STOP_LISTEN" || word == "DPP_BOOTSTRAP_REMOVE") return "OK\n";
    return "UNKNOWN COMMAND\n";
  }

  std::filesystem::path _path;
  int _fd = -1;
  std::thread _thread;
  std::atomic<bool> _stop{false};
  std::atomic<bool> _without_dpp{false};
  std::atomic<bool> _refuse_listen{false};
  std::mutex _mutex;
  std::string _silent_on;
  std::vector<std::string> _commands;
  sockaddr_un _attached{};
  socklen_t _attached_length = 0;
};

class EasyConnectWpaSupplicantTest : public ::testing::Test {
 protected:
  void SetUp() override {
    wifi::EasyConnectWpaSupplicant::Options options;
    options.control_dir = dir.path().string();
    options.interface = "wlan0";
    enrollee = std::make_shared<wifi::EasyConnectWpaSupplicant>(logger, daemon.executor(), timers, options);
  }

  void TearDown() override {
    daemon.run([this]() {
      enrollee->stop();
      enrollee.reset();
    });
  }

  types::Result<std::string> start() {
    Awaited<types::Result<std::string>> started;
    auto on_started = started.handler();
    auto on_received = received.handler();
    daemon.run([&]() { enrollee->start(daemon.executor(), on_started, on_received); });

    types::Result<std::string> out;
    EXPECT_TRUE(started.wait(out)) << "start was never answered\n" << logger->text();
    return out;
  }

  void advance(std::chrono::milliseconds by) {
    daemon.run([&]() { timers->advance(by); });
  }

  bool cleaned_up() { return wpa->received("DPP_STOP_LISTEN") && wpa->received("DPP_BOOTSTRAP_REMOVE 1") && !wpa->attached(); }

  TempDir dir{"wpa"};
  std::unique_ptr<FakeWpaSupplicant> wpa = std::make_unique<FakeWpaSupplicant>(dir.path() / "wlan0");
  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  std::shared_ptr<timers::ManualTimerSource> timers = std::make_shared<timers::ManualTimerSource>();
  DaemonThread daemon;
  std::shared_ptr<wifi::EasyConnectWpaSupplicant> enrollee;
  Awaited<types::Result<wifi::EasyConnectCredentials>> received;
};

}  // namespace

TEST_F(EasyConnectWpaSupplicantTest, StartsWithTheProtocolsCommandsAndAnswersTheUri) {
  const auto uri = start();
  ASSERT_TRUE(uri.ok) << uri.error;
  EXPECT_EQ(uri.value, "DPP:C:81/1,81/6,81/11;M:88a29e23c4b4;V:2;K:MDkwEwYHKoZIzj0CAQ;;");

  // docs/protocol.md: channels 81/1, 81/6, 81/11 and the interface's MAC; listen on 2437.
  const std::vector<std::string> expected{
      "ATTACH", "STATUS", "DPP_BOOTSTRAP_GEN type=qrcode chan=81/1,81/6,81/11 mac=88a29e23c4b4", "DPP_BOOTSTRAP_GET_URI 1", "DPP_LISTEN 2437 role=enrollee",
  };
  EXPECT_EQ(wpa->commands(), expected);
  EXPECT_TRUE(wpa->attached());
  EXPECT_TRUE(daemon.run([this]() { return enrollee->active(); }));
}

TEST_F(EasyConnectWpaSupplicantTest, ANetworkFromThePhoneIsReceivedAndEverythingIsUndone) {
  ASSERT_TRUE(start().ok);

  wpa->emit("DPP-AUTH-SUCCESS init=0");
  wpa->emit("DPP-CONF-RECEIVED ");
  wpa->emit("DPP-CONFOBJ-AKM psk");
  wpa->emit("DPP-CONFOBJ-SSID Tom's \\\"Home\\\"");  // wpa_ssid_txt escapes the double quotes
  wpa->emit("DPP-CONFOBJ-PASS 636f727265637420686f727365");

  types::Result<wifi::EasyConnectCredentials> credentials;
  ASSERT_TRUE(received.wait(credentials)) << logger->text();
  ASSERT_TRUE(credentials.ok) << credentials.error;
  EXPECT_EQ(credentials.value.ssid, "Tom's \"Home\"");
  EXPECT_EQ(credentials.value.passphrase, "correct horse");
  EXPECT_EQ(credentials.value.akm, "psk");

  EXPECT_TRUE(eventually([this]() { return cleaned_up(); })) << "listening stopped, the key removed, events detached";
  EXPECT_FALSE(daemon.run([this]() { return enrollee->active(); }));
}

TEST_F(EasyConnectWpaSupplicantTest, ARawPskIsPassedOnAsItsHex) {
  ASSERT_TRUE(start().ok);

  const std::string psk(64, 'a');
  wpa->emit("DPP-CONF-RECEIVED ");
  wpa->emit("DPP-CONFOBJ-AKM psk");
  wpa->emit("DPP-CONFOBJ-SSID Home");
  wpa->emit("DPP-CONFOBJ-PSK " + psk);

  types::Result<wifi::EasyConnectCredentials> credentials;
  ASSERT_TRUE(received.wait(credentials));
  ASSERT_TRUE(credentials.ok) << credentials.error;
  EXPECT_EQ(credentials.value.passphrase, psk);
}

TEST_F(EasyConnectWpaSupplicantTest, AFailedConfigurationEndsIt) {
  ASSERT_TRUE(start().ok);
  wpa->emit("DPP-CONF-FAILED ");

  types::Result<wifi::EasyConnectCredentials> credentials;
  ASSERT_TRUE(received.wait(credentials));
  EXPECT_FALSE(credentials.ok);
  EXPECT_NE(credentials.error.find("DPP-CONF-FAILED"), std::string::npos) << credentials.error;
  EXPECT_TRUE(eventually([this]() { return cleaned_up(); }));
}

TEST_F(EasyConnectWpaSupplicantTest, AConfigurationWithoutAPassphraseFailsAfterItSettles) {
  ASSERT_TRUE(start().ok);
  wpa->emit("DPP-CONF-RECEIVED ");
  wpa->emit("DPP-CONFOBJ-AKM dpp");
  wpa->emit("DPP-CONFOBJ-SSID Office");
  ASSERT_TRUE(eventually([this]() { return logger->contains("a configuration arrived"); }));

  // The configuration's events are all in; let the settle time pass.
  std::this_thread::sleep_for(50ms);
  advance(2s);

  types::Result<wifi::EasyConnectCredentials> credentials;
  ASSERT_TRUE(received.wait(credentials));
  EXPECT_FALSE(credentials.ok);
  EXPECT_NE(credentials.error.find("AKM dpp"), std::string::npos) << credentials.error;
}

TEST_F(EasyConnectWpaSupplicantTest, NobodyWithin120SecondsEndsIt) {
  ASSERT_TRUE(start().ok);

  advance(119s);
  EXPECT_TRUE(daemon.run([this]() { return enrollee->active(); }));
  advance(1s);

  types::Result<wifi::EasyConnectCredentials> credentials;
  ASSERT_TRUE(received.wait(credentials));
  EXPECT_FALSE(credentials.ok);
  EXPECT_NE(credentials.error.find("120 s"), std::string::npos) << credentials.error;
  EXPECT_TRUE(eventually([this]() { return cleaned_up(); }));
}

TEST_F(EasyConnectWpaSupplicantTest, StopUndoesEverythingAndReportsNothing) {
  ASSERT_TRUE(start().ok);

  daemon.run([this]() { enrollee->stop(); });
  EXPECT_TRUE(eventually([this]() { return cleaned_up(); }));

  // An event after the stop goes nowhere.
  wpa->emit("DPP-CONF-FAILED ");
  types::Result<wifi::EasyConnectCredentials> credentials;
  EXPECT_FALSE(received.wait(credentials, 200ms));
}

TEST_F(EasyConnectWpaSupplicantTest, NoControlSocketIsEasyconnectUnavailable) {
  wpa.reset();

  const auto uri = start();
  EXPECT_FALSE(uri.ok);
  EXPECT_EQ(uri.code, protocol::errors::easyconnect_unavailable);
  EXPECT_NE(uri.error.find("no wpa_supplicant control socket"), std::string::npos) << uri.error;
}

TEST_F(EasyConnectWpaSupplicantTest, AWpaSupplicantWithoutDppIsEasyconnectUnavailable) {
  wpa->without_dpp();

  const auto uri = start();
  EXPECT_FALSE(uri.ok);
  EXPECT_EQ(uri.code, protocol::errors::easyconnect_unavailable);
  EXPECT_NE(uri.error.find("without DPP"), std::string::npos) << uri.error;
}

TEST_F(EasyConnectWpaSupplicantTest, ARefusedListenRemovesTheKeyItMade) {
  wpa->refuse_listen();

  const auto uri = start();
  EXPECT_FALSE(uri.ok);
  EXPECT_EQ(uri.code, protocol::errors::easyconnect_unavailable);
  EXPECT_TRUE(eventually([this]() { return wpa->received("DPP_BOOTSTRAP_REMOVE 1") && !wpa->attached(); }));
}

TEST_F(EasyConnectWpaSupplicantTest, AWpaSupplicantThatDoesNotAnswerIsEasyconnectUnavailable) {
  wpa->silent_on("STATUS");

  Awaited<types::Result<std::string>> started;
  auto on_started = started.handler();
  auto on_received = received.handler();
  daemon.run([&]() { enrollee->start(daemon.executor(), on_started, on_received); });
  ASSERT_TRUE(eventually([this]() { return wpa->received("STATUS"); }));

  advance(3s);

  types::Result<std::string> uri;
  ASSERT_TRUE(started.wait(uri));
  EXPECT_FALSE(uri.ok);
  EXPECT_NE(uri.error.find("did not answer STATUS"), std::string::npos) << uri.error;
}
