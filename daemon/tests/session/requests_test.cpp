//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "session/requests.h"

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <memory>
#include <optional>
#include <string>

#include "mocks/logger_mock.h"
#include "protocol/json.h"
#include "system/uninstaller_fake.h"
#include "wifi/easy_connect_fake.h"
#include "wifi/wifi_fake.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

struct Fixture {
  boost::asio::io_context io;
  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  std::shared_ptr<wifi::WifiFake> wifi = std::make_shared<wifi::WifiFake>(logger);
  std::shared_ptr<wifi::EasyConnectFake> easyconnect = std::make_shared<wifi::EasyConnectFake>(logger);
  session::Requests requests{logger, wifi, "testpi", easyconnect};

  void drain() {
    io.restart();
    io.run();
  }

  // Handles one request and gives back the response, parsed.
  Json handle(const std::string& json) {
    auto request = parse_request(json);
    EXPECT_TRUE(request) << request.error;

    std::optional<std::string> response;
    bool inline_answer = true;
    requests.handle(request.value, io.get_executor(), [&](std::string text) {
      EXPECT_FALSE(inline_answer) << "answered inline rather than on the executor";
      response = text;
    });
    inline_answer = false;

    io.restart();
    io.run();

    EXPECT_TRUE(response) << "no response";
    auto parsed = Json::parse(response.value_or("null"));
    EXPECT_TRUE(parsed) << *response;
    return parsed.value_or(Json());
  }
};

}  // namespace

TEST(RequestsTest, StatusReportsTheHostnameAndTheWifi) {
  Fixture f;
  wifi::WifiStatus status;
  status.connected = true;
  status.ssid = "home";
  status.ip = "10.0.0.5";
  f.wifi->set_status(status);

  const auto response = f.handle("{\"op\":\"status\"}");
  EXPECT_TRUE(response.get("ok")->as_boolean());
  EXPECT_EQ(response.get("hostname")->as_string(), "testpi");
  EXPECT_TRUE(response.get("wifi")->get("connected")->as_boolean());
  EXPECT_EQ(response.get("wifi")->get("ssid")->as_string(), "home");
  EXPECT_EQ(response.get("wifi")->get("ip")->as_string(), "10.0.0.5");

  f.wifi->set_status({});
  const auto down = f.handle("{\"op\":\"status\"}");
  EXPECT_FALSE(down.get("wifi")->get("connected")->as_boolean());
  EXPECT_EQ(down.get("wifi")->get("ssid"), nullptr);
}

TEST(RequestsTest, ScanListsTheNetworksStrongestFirst) {
  Fixture f;
  f.wifi->set_networks({{"weak", 20, true}, {"strong", 90, false}});

  const auto response = f.handle("{\"op\":\"wifi.scan\"}");
  EXPECT_TRUE(response.get("ok")->as_boolean());
  const auto& networks = response.get("networks")->items();
  ASSERT_EQ(networks.size(), 2u);
  EXPECT_EQ(networks[0].get("ssid")->as_string(), "strong");
  EXPECT_EQ(networks[1].get("ssid")->as_string(), "weak");
}

TEST(RequestsTest, ConfigureHandsTheSsidAndPskToTheBackend) {
  Fixture f;

  const auto response = f.handle("{\"op\":\"wifi.configure\",\"ssid\":\"home\",\"psk\":\"secret\"}");
  EXPECT_TRUE(response.get("ok")->as_boolean());
  EXPECT_EQ(f.wifi->configures(), 1);
  EXPECT_EQ(f.wifi->configured_ssid(), "home");
  EXPECT_EQ(f.wifi->configured_psk(), "secret");

  const auto open = f.handle("{\"op\":\"wifi.configure\",\"ssid\":\"cafe\"}");
  EXPECT_TRUE(open.get("ok")->as_boolean());
  EXPECT_EQ(f.wifi->configured_psk(), "");
}

TEST(RequestsTest, ForgetAsksTheBackend) {
  Fixture f;
  const auto response = f.handle("{\"op\":\"wifi.forget\"}");
  EXPECT_TRUE(response.get("ok")->as_boolean());
  EXPECT_EQ(f.wifi->forgets(), 1);
}

TEST(RequestsTest, ABackendFailureIsTheProtocolsErrorWithItsReason) {
  Fixture f;

  f.wifi->fail(errors::wifi_unavailable, "NetworkManager is not answering");
  auto response = f.handle("{\"op\":\"wifi.scan\"}");
  EXPECT_FALSE(response.get("ok")->as_boolean());
  EXPECT_EQ(response.get("error")->as_string(), "wifi_unavailable");
  EXPECT_EQ(response.get("message")->as_string(), "NetworkManager is not answering");

  f.wifi->fail(errors::wifi_failed, "bad passphrase");
  response = f.handle("{\"op\":\"wifi.configure\",\"ssid\":\"home\",\"psk\":\"wrong\"}");
  EXPECT_EQ(response.get("error")->as_string(), "wifi_failed");
  EXPECT_EQ(response.get("message")->as_string(), "bad passphrase");

  // A failure with no code is a refusal.
  f.wifi->fail("", "something");
  response = f.handle("{\"op\":\"wifi.forget\"}");
  EXPECT_EQ(response.get("error")->as_string(), "wifi_failed");

  EXPECT_EQ(session::wifi_error_code(""), "wifi_failed");
  EXPECT_EQ(session::wifi_error_code("wifi_unavailable"), "wifi_unavailable");
}

// --- Wi-Fi Easy Connect (docs/protocol.md, "Copying the phone's WiFi") ---------------

TEST(RequestsTest, EasyConnectAnswersTheBootstrapUri) {
  Fixture f;
  const auto response = f.handle("{\"op\":\"wifi.easyconnect\"}");
  ASSERT_TRUE(response.get("ok")->as_boolean()) << response.dump();
  EXPECT_EQ(response.get("uri")->as_string().rfind("DPP:", 0), 0u);
  EXPECT_TRUE(f.easyconnect->active());
}

TEST(RequestsTest, TheNetworkAPhoneSendsIsConfiguredThroughTheWifiBackend) {
  Fixture f;
  ASSERT_TRUE(f.handle("{\"op\":\"wifi.easyconnect\"}").get("ok")->as_boolean());

  f.easyconnect->deliver(wifi::EasyConnectCredentials{"home", "correct horse", "psk"});
  f.drain();

  EXPECT_EQ(f.wifi->configures(), 1);
  EXPECT_EQ(f.wifi->configured_ssid(), "home");
  EXPECT_EQ(f.wifi->configured_psk(), "correct horse");
}

TEST(RequestsTest, AFailedExchangeConfiguresNothing) {
  Fixture f;
  ASSERT_TRUE(f.handle("{\"op\":\"wifi.easyconnect\"}").get("ok")->as_boolean());

  f.easyconnect->fail_exchange("no phone sent a network within 120 s");
  f.drain();

  EXPECT_EQ(f.wifi->configures(), 0);
  EXPECT_TRUE(f.logger->contains("no phone sent a network")) << f.logger->text();
}

TEST(RequestsTest, EasyConnectThatCannotStartIsEasyconnectUnavailable) {
  Fixture f;
  f.easyconnect->fail_start("this wpa_supplicant was built without DPP");

  const auto response = f.handle("{\"op\":\"wifi.easyconnect\"}");
  EXPECT_FALSE(response.get("ok")->as_boolean());
  EXPECT_EQ(response.get("error")->as_string(), "easyconnect_unavailable");
  EXPECT_EQ(response.get("message")->as_string(), "this wpa_supplicant was built without DPP");
}

TEST(RequestsTest, EasyConnectOffIsEasyconnectUnavailable) {
  boost::asio::io_context io;
  auto logger = std::make_shared<MockLogger>();
  session::Requests requests(logger, std::make_shared<wifi::WifiFake>(logger), "testpi");

  const auto request = parse_request("{\"op\":\"wifi.easyconnect\"}");
  ASSERT_TRUE(request);

  std::string response;
  requests.handle(request.value, io.get_executor(), [&](std::string text) { response = text; });
  io.run();

  const auto parsed = Json::parse(response);
  ASSERT_TRUE(parsed) << response;
  EXPECT_EQ(parsed->get("error")->as_string(), "easyconnect_unavailable");
}

TEST(RequestsTest, EasyConnectStopsOnRequestAndWhenTheSessionCloses) {
  Fixture f;

  ASSERT_TRUE(f.handle("{\"op\":\"wifi.easyconnect\"}").get("ok")->as_boolean());
  EXPECT_TRUE(f.handle("{\"op\":\"wifi.easyconnect.stop\"}").get("ok")->as_boolean());
  EXPECT_FALSE(f.easyconnect->active());
  EXPECT_EQ(f.easyconnect->stops(), 1);

  EXPECT_TRUE(f.handle("{\"op\":\"wifi.easyconnect.stop\"}").get("ok")->as_boolean()) << "stopping nothing is ok";

  ASSERT_TRUE(f.handle("{\"op\":\"wifi.easyconnect\"}").get("ok")->as_boolean());
  f.requests.closed();
  EXPECT_FALSE(f.easyconnect->active());
  EXPECT_EQ(f.easyconnect->stops(), 2);

  // Nothing arrives after a stop.
  f.easyconnect->deliver(wifi::EasyConnectCredentials{"home", "correct horse", "psk"});
  f.drain();
  EXPECT_EQ(f.wifi->configures(), 0);
}

TEST(RequestsTest, FinishIsOkAndAnUnknownOpSaysSo) {
  Fixture f;
  EXPECT_TRUE(f.handle("{\"op\":\"finish\"}").get("ok")->as_boolean());

  const auto unknown = f.handle("{\"op\":\"reboot\"}");
  EXPECT_FALSE(unknown.get("ok")->as_boolean());
  EXPECT_EQ(unknown.get("error")->as_string(), "unknown_op");
  EXPECT_NE(unknown.get("message")->as_string().find("reboot"), std::string::npos);
}

// docs/protocol.md, Uninstalling.
TEST(RequestsTest, UninstallStartsTheUninstallerAndAnswersOk) {
  Fixture f;
  auto uninstaller = std::make_shared<system::UninstallerFake>(f.logger);
  session::Requests requests{f.logger, f.wifi, "testpi", f.easyconnect, uninstaller};

  std::optional<std::string> response;
  requests.handle(parse_request("{\"op\":\"uninstall\"}").value, f.io.get_executor(), [&](std::string text) { response = text; });
  f.drain();

  ASSERT_TRUE(response);
  EXPECT_EQ(*response, "{\"ok\":true}");
  EXPECT_EQ(uninstaller->starts(), 1);
}

TEST(RequestsTest, UninstallWithNoUninstallerIsUnavailable) {
  Fixture f;

  const auto response = f.handle("{\"op\":\"uninstall\"}");
  EXPECT_FALSE(response.get("ok")->as_boolean());
  EXPECT_EQ(response.get("error")->as_string(), errors::uninstall_unavailable);
}

TEST(RequestsTest, AnUninstallerThatFailsIsUnavailableWithItsReason) {
  Fixture f;
  auto uninstaller = std::make_shared<system::UninstallerFake>(f.logger);
  uninstaller->fail_start("Unit rpiledconfig-uninstall.service not found.");
  session::Requests requests{f.logger, f.wifi, "testpi", f.easyconnect, uninstaller};

  std::optional<std::string> response;
  requests.handle(parse_request("{\"op\":\"uninstall\"}").value, f.io.get_executor(), [&](std::string text) { response = text; });
  f.drain();

  ASSERT_TRUE(response);
  EXPECT_NE(response->find("\"error\":\"uninstall_unavailable\""), std::string::npos) << *response;
  EXPECT_NE(response->find("not found"), std::string::npos) << *response;
}
