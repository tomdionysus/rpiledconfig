//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "wifi/wifi_networkmanager.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>

#include "dbus_test_helper.h"
#include "mock_networkmanager.h"
#include "mocks/logger_mock.h"
#include "protocol/messages.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

using AP = MockNetworkManager::AccessPoint;

// NM_802_11_AP_SEC_KEY_MGMT_PSK | PAIR_CCMP | GROUP_CCMP, as a WPA2 network shows.
constexpr std::uint32_t rsn_wpa2 = 0x100 | 0x8 | 0x80;

std::vector<AP> neighbourhood() {
  return {
      AP{"Home", 70, 0x1, 0, rsn_wpa2}, AP{"Cafe", 40, 0, 0, 0}, AP{"Old", 30, 0x1, 0, 0}, AP{"", 90, 0x1, 0, rsn_wpa2}, AP{"Home", 20, 0x1, 0, rsn_wpa2},
  };
}

const wifi::WifiNetwork* find(const std::vector<wifi::WifiNetwork>& networks, const std::string& ssid) {
  for (const auto& n : networks) {
    if (n.ssid == ssid) return &n;
  }
  return nullptr;
}

class WifiNetworkManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    REQUIRE_TEST_BUS();
    nm = std::make_unique<MockNetworkManager>(test_bus_address(), neighbourhood(), with_wifi_device);
    if (with_nm) {
      ASSERT_TRUE(nm->start());
    }

    loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor(), test_bus_address());
    ASSERT_TRUE(daemon.run([this]() { return loop->open(); }).ok);

    wifi::WifiNetworkManager::Options options;
    options.scan_timeout = scan_timeout;
    wifi = std::make_shared<wifi::WifiNetworkManager>(logger, loop, options);
  }

  void TearDown() override {
    if (wifi) {
      daemon.run([this]() {
        wifi.reset();
        loop->close();
      });
    }
    nm.reset();
  }

  types::Result<wifi::WifiStatus> status() {
    Awaited<types::Result<wifi::WifiStatus>> answer;
    wifi->status(daemon.executor(), answer.handler());
    types::Result<wifi::WifiStatus> out;
    EXPECT_TRUE(answer.wait(out)) << "no answer to status";
    return out;
  }

  types::Result<std::vector<wifi::WifiNetwork>> scan() {
    Awaited<types::Result<std::vector<wifi::WifiNetwork>>> answer;
    wifi->scan(daemon.executor(), answer.handler());
    types::Result<std::vector<wifi::WifiNetwork>> out;
    EXPECT_TRUE(answer.wait(out)) << "no answer to scan";
    return out;
  }

  types::Status configure(const std::string& ssid, const std::string& psk) {
    Awaited<types::Status> answer;
    wifi->configure(ssid, psk, false, daemon.executor(), answer.handler());
    types::Status out;
    EXPECT_TRUE(answer.wait(out)) << "no answer to configure";
    return out;
  }

  types::Result<int> forget() {
    Awaited<types::Result<int>> answer;
    wifi->forget(daemon.executor(), answer.handler());
    types::Result<int> out;
    EXPECT_TRUE(answer.wait(out)) << "no answer to forget";
    return out;
  }

  std::vector<MockNetworkManager::Saved> ours() {
    auto all = nm->connections();
    std::vector<MockNetworkManager::Saved> out;
    std::copy_if(all.begin(), all.end(), std::back_inserter(out), [](const auto& c) { return c.id.rfind("rpiledconfig-", 0) == 0; });
    return out;
  }

  bool with_nm = true;
  bool with_wifi_device = true;
  std::chrono::milliseconds scan_timeout = 2s;
  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  DaemonThread daemon;
  std::unique_ptr<MockNetworkManager> nm;
  std::shared_ptr<dbus::SdBusLoop> loop;
  std::shared_ptr<wifi::WifiNetworkManager> wifi;
};

}  // namespace

// --- status --------------------------------------------------------------------------

TEST_F(WifiNetworkManagerTest, NoActiveConnectionIsNotConnected) {
  const auto result = status();
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_FALSE(result.value.connected);
  EXPECT_TRUE(result.value.ssid.empty());
  EXPECT_TRUE(result.value.ip.empty());
}

TEST_F(WifiNetworkManagerTest, AnActivatedConnectionIsConnectedWithItsSsidAndAddress) {
  nm->connect_existing("netplan-wlan0-Home", "Home", "10.0.0.7");

  const auto result = status();
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_TRUE(result.value.connected);
  EXPECT_EQ(result.value.ssid, "Home") << "the SSID, not the connection's name";
  EXPECT_EQ(result.value.ip, "10.0.0.7");
}

TEST_F(WifiNetworkManagerTest, AConnectionStillActivatingIsNotConnectedYet) {
  nm->activation_hangs();
  ASSERT_TRUE(configure("Home", "correct horse").ok);

  const auto result = status();
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_FALSE(result.value.connected);
}

// --- scan ----------------------------------------------------------------------------

TEST_F(WifiNetworkManagerTest, AScanIsAskedForAndWaitedFor) {
  nm->found_by_scan(AP{"New", 55, 0, 0, rsn_wpa2});

  const auto result = scan();
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(nm->scans_requested(), 1);
  EXPECT_NE(find(result.value, "New"), nullptr) << "found by the scan, so the answer came after it";
  EXPECT_EQ(result.value.size(), 6u) << "every access point; one per SSID is the response's job";
}

TEST_F(WifiNetworkManagerTest, SecureFollowsTheAccessPointsFlags) {
  const auto result = scan();
  ASSERT_TRUE(result.ok) << result.error;

  ASSERT_NE(find(result.value, "Cafe"), nullptr);
  EXPECT_FALSE(find(result.value, "Cafe")->secure) << "no privacy, no WPA, no RSN";
  EXPECT_TRUE(find(result.value, "Old")->secure) << "privacy alone (WEP)";
  EXPECT_TRUE(find(result.value, "Home")->secure) << "RSN";
  EXPECT_EQ(find(result.value, "Cafe")->signal, 40);
}

TEST_F(WifiNetworkManagerTest, ARefusedScanAnswersWithWhatNetworkManagerAlreadyHas) {
  nm->set_scan(MockNetworkManager::Scan::refused);
  nm->found_by_scan(AP{"New", 55, 0, 0, rsn_wpa2});

  const auto result = scan();
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(find(result.value, "New"), nullptr);
  EXPECT_NE(find(result.value, "Home"), nullptr);
}

class WifiNetworkManagerShortScanTest : public WifiNetworkManagerTest {
 protected:
  void SetUp() override {
    scan_timeout = 300ms;
    WifiNetworkManagerTest::SetUp();
  }
};

TEST_F(WifiNetworkManagerShortScanTest, AScanThatNeverFinishesIsAnsweredAfterTheTimeout) {
  nm->set_scan(MockNetworkManager::Scan::never_completes);

  const auto started = std::chrono::steady_clock::now();
  const auto result = scan();
  const auto took = std::chrono::steady_clock::now() - started;

  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_NE(find(result.value, "Home"), nullptr);
  EXPECT_GE(took, 250ms);
  EXPECT_LT(took, 2s);
}

// --- configure -----------------------------------------------------------------------

TEST_F(WifiNetworkManagerTest, ConfigureSavesAWpaPskConnectionAndActivatesIt) {
  const auto configured = configure("Home", "correct horse");
  ASSERT_TRUE(configured.ok) << configured.error;

  const auto saved = ours();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved[0].id, "rpiledconfig-Home");
  EXPECT_EQ(saved[0].type, "802-11-wireless");
  EXPECT_EQ(saved[0].ssid, "Home");
  EXPECT_EQ(saved[0].key_mgmt, "wpa-psk");
  EXPECT_EQ(saved[0].psk, "correct horse");

  EXPECT_TRUE(eventually([this]() {
    const auto s = status();
    return s.ok && s.value.connected && s.value.ssid == "Home" && s.value.ip == "192.0.2.50";
  }));
  EXPECT_TRUE(eventually([this]() { return logger->contains("Joined Home"); })) << logger->text();
}

TEST_F(WifiNetworkManagerTest, AnOpenNetworkHasNoSecuritySettings) {
  ASSERT_TRUE(configure("Cafe", "").ok);

  const auto saved = ours();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved[0].ssid, "Cafe");
  EXPECT_TRUE(saved[0].key_mgmt.empty());
}

TEST_F(WifiNetworkManagerTest, NetworkManagersRefusalIsWifiFailedWithItsReason) {
  const auto configured = configure("Home", "short");
  EXPECT_FALSE(configured.ok);
  EXPECT_EQ(configured.code, protocol::errors::wifi_failed);
  EXPECT_NE(configured.error.find("802-11-wireless-security.psk: property is invalid"), std::string::npos) << configured.error;
  EXPECT_TRUE(ours().empty());
}

TEST_F(WifiNetworkManagerTest, PermissionDeniedIsWifiFailed) {
  nm->refuse_add("org.freedesktop.NetworkManager.PermissionDenied", "Insufficient privileges");
  const auto configured = configure("Home", "correct horse");
  EXPECT_FALSE(configured.ok);
  EXPECT_EQ(configured.code, protocol::errors::wifi_failed);
  EXPECT_NE(configured.error.find("Insufficient privileges"), std::string::npos) << configured.error;
}

// docs/protocol.md, The latest join: status says what became of the configure, as soon as
// NetworkManager has said.
TEST_F(WifiNetworkManagerTest, StatusCarriesTheLatestJoinAndItsReason) {
  EXPECT_FALSE(status().value.join.has_value()) << "nothing configured yet";

  nm->activation_hangs();
  ASSERT_TRUE(configure("Home", "correct horse").ok);
  auto joining = status();
  ASSERT_TRUE(joining.ok && joining.value.join.has_value());
  EXPECT_EQ(joining.value.join->ssid, "Home");
  EXPECT_EQ(joining.value.join->state, "joining");

  nm->activation_hangs(false);
  nm->activation_fails_with(7);
  ASSERT_TRUE(configure("Home", "wrong passphrase").ok);
  EXPECT_TRUE(eventually([this]() { return status().value.join->state == "failed"; }));
  auto failed = status();
  EXPECT_EQ(failed.value.join->reason, "wrong_passphrase");
  EXPECT_NE(failed.value.join->message.find("no secrets"), std::string::npos) << failed.value.join->message;
  EXPECT_FALSE(failed.value.connected);

  nm->activation_fails_with(53);
  ASSERT_TRUE(configure("Attic", "correct horse").ok);
  EXPECT_TRUE(eventually([this]() {
    const auto s = status();
    return s.ok && s.value.join && s.value.join->ssid == "Attic" && s.value.join->state == "failed";
  }));
  EXPECT_EQ(status().value.join->reason, "not_found");

  nm->activation_succeeds(true);
  ASSERT_TRUE(configure("Home", "correct horse").ok);
  EXPECT_TRUE(eventually([this]() { return status().value.join->state == "joined"; }));
  EXPECT_TRUE(status().value.connected);
}

// docs/protocol.md, The latest join: a failure NetworkManager gives another reason for is
// `other`, with the reason in NetworkManager's words (NMDeviceStateReason) in the message.
TEST_F(WifiNetworkManagerTest, AJoinThatFailsForAnotherReasonIsOtherInItsOwnWords) {
  const std::pair<std::uint32_t, std::string> reasons[] = {
      {8, "the supplicant disconnected"}, {9, "the supplicant's configuration failed"},
      {10, "the supplicant failed"},      {11, "the supplicant timed out"},
      {15, "the DHCP lease failed"},      {99, "reason 99"},
  };
  for (const auto& [reason, words] : reasons) {
    nm->activation_fails_with(reason);
    const std::string ssid = "Net" + std::to_string(reason);
    ASSERT_TRUE(configure(ssid, "correct horse").ok);
    ASSERT_TRUE(eventually([&]() {
      const auto s = status();
      return s.ok && s.value.join && s.value.join->ssid == ssid && s.value.join->state == "failed";
    })) << reason;
    const auto join = *status().value.join;
    EXPECT_EQ(join.reason, "other") << reason;
    EXPECT_EQ(join.message, "NetworkManager: " + words);
  }
  EXPECT_EQ(wifi->describe(), "networkmanager");
}

// A typo must not cost a Pi in the field the network it could get back to.
TEST_F(WifiNetworkManagerTest, AFailedJoinLeavesTheNetworkThatWorkedAndDeletesOnlyItself) {
  ASSERT_TRUE(configure("Home", "correct horse").ok);
  ASSERT_TRUE(eventually([this]() { return status().value.join->state == "joined"; }));

  nm->activation_fails_with(7);
  ASSERT_TRUE(configure("Attic", "wrong passphrase").ok) << "ok once asked for, whatever happens next";
  ASSERT_TRUE(eventually([this]() { return logger->contains("Could not join Attic"); })) << logger->text();

  EXPECT_TRUE(eventually([this]() {
    const auto saved = ours();
    return saved.size() == 1 && saved[0].psk == "correct horse";
  }));
  EXPECT_EQ(ours().at(0).id, "rpiledconfig-Home");
}

TEST_F(WifiNetworkManagerTest, ANetworkStillJoiningDeletesNothing) {
  ASSERT_TRUE(configure("Home", "correct horse").ok);
  ASSERT_TRUE(eventually([this]() { return status().value.join->state == "joined"; }));

  nm->activation_hangs();
  ASSERT_TRUE(configure("Attic", "correct horse").ok);
  EXPECT_EQ(ours().size(), 2u);
  EXPECT_EQ(nm->deletes(), 0);
}

TEST_F(WifiNetworkManagerTest, ANewNetworkThatJoinsReplacesTheEarlierOne) {
  ASSERT_TRUE(configure("Home", "correct horse").ok);
  ASSERT_TRUE(eventually([this]() { return status().value.join->state == "joined"; }));

  ASSERT_TRUE(configure("Attic", "battery staple").ok);
  EXPECT_TRUE(eventually([this]() {
    const auto saved = ours();
    return saved.size() == 1 && saved[0].id == "rpiledconfig-Attic";
  }));
}

// --- forget --------------------------------------------------------------------------

TEST_F(WifiNetworkManagerTest, ForgetDeletesOnlyTheConnectionsThisDaemonMade) {
  nm->connect_existing("netplan-wlan0-Neighbour", "Neighbour", "10.0.0.8");
  ASSERT_TRUE(configure("Home", "correct horse").ok);
  ASSERT_EQ(ours().size(), 1u);

  const auto forgotten = forget();
  ASSERT_TRUE(forgotten.ok) << forgotten.error;
  EXPECT_EQ(forgotten.value, 1) << "it says how many";
  EXPECT_TRUE(ours().empty());
  EXPECT_EQ(nm->deletes(), 1);

  const auto all = nm->connections();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].id, "netplan-wlan0-Neighbour");
}

TEST_F(WifiNetworkManagerTest, ForgettingNothingIsOk) {
  const auto forgotten = forget();
  EXPECT_TRUE(forgotten.ok) << forgotten.error;
  EXPECT_EQ(forgotten.value, 0) << "and says so, for the app to tell the user nothing changed";
  EXPECT_EQ(nm->deletes(), 0);
}

// --- NetworkManager not there --------------------------------------------------------

class WifiWithoutNetworkManagerTest : public WifiNetworkManagerTest {
 protected:
  void SetUp() override {
    with_nm = false;
    WifiNetworkManagerTest::SetUp();
  }
};

TEST_F(WifiWithoutNetworkManagerTest, EveryCallIsWifiUnavailable) {
  EXPECT_EQ(status().code, protocol::errors::wifi_unavailable);
  EXPECT_EQ(scan().code, protocol::errors::wifi_unavailable);
  EXPECT_EQ(configure("Home", "correct horse").code, protocol::errors::wifi_unavailable);
  EXPECT_EQ(forget().code, protocol::errors::wifi_unavailable);
}

class WifiWithoutAWifiDeviceTest : public WifiNetworkManagerTest {
 protected:
  void SetUp() override {
    with_wifi_device = false;
    WifiNetworkManagerTest::SetUp();
  }
};

TEST_F(WifiWithoutAWifiDeviceTest, StatusAndScanAreWifiUnavailable) {
  const auto s = status();
  EXPECT_FALSE(s.ok);
  EXPECT_EQ(s.code, protocol::errors::wifi_unavailable);
  EXPECT_NE(s.error.find("no WiFi device"), std::string::npos) << s.error;
  EXPECT_EQ(scan().code, protocol::errors::wifi_unavailable);
}

// --- The real NetworkManager, read only ----------------------------------------------
//
// For a Linux host where the real NetworkManager may be asked, never told: status and
// scan only (a scan the host's polkit refuses is answered from NetworkManager's cache,
// which is the fallback under test). Off unless RPILEDCONFIG_TEST_SYSTEM_NM=1.

TEST(WifiNetworkManagerSystemTest, StatusAndScanAgainstTheRealNetworkManager) {
  const char* enabled = std::getenv("RPILEDCONFIG_TEST_SYSTEM_NM");
  if (enabled == nullptr || std::string(enabled) != "1") GTEST_SKIP() << "RPILEDCONFIG_TEST_SYSTEM_NM is not 1";

  auto logger = std::make_shared<MockLogger>();
  DaemonThread daemon;
  auto loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor());
  ASSERT_TRUE(daemon.run([&]() { return loop->open(); }).ok);
  auto wifi = std::make_shared<wifi::WifiNetworkManager>(logger, loop);

  Awaited<types::Result<wifi::WifiStatus>> status;
  wifi->status(daemon.executor(), status.handler());
  types::Result<wifi::WifiStatus> s;
  ASSERT_TRUE(status.wait(s));
  ASSERT_TRUE(s.ok) << s.error;
  std::cout << "status: connected " << s.value.connected << ", ssid " << s.value.ssid << ", ip " << s.value.ip << "\n";

  Awaited<types::Result<std::vector<wifi::WifiNetwork>>> scan;
  wifi->scan(daemon.executor(), scan.handler());
  types::Result<std::vector<wifi::WifiNetwork>> n;
  ASSERT_TRUE(scan.wait(n, 20s));
  ASSERT_TRUE(n.ok) << n.error;
  std::cout << protocol::scan_response(n.value) << "\n";
  std::cout << logger->text();

  daemon.run([&]() {
    wifi.reset();
    loop->close();
  });
}
