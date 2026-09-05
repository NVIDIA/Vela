// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "NetworkTestHelpers.h"
#include "catch.hpp"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// std
#include <atomic>
#include <chrono>
#include <memory>

using namespace vsr::network;
using namespace std::chrono_literals;

namespace {

// The span within which a burst of read/write errors on one connection
// lands, so a handler that fired once must not fire again during it.
constexpr auto ERROR_BURST = 50ms;

struct LifecycleCounters
{
  void attach(NetworkChannel &channel);

  std::atomic<int> connected{0};
  std::atomic<int> disconnected{0};
};

void LifecycleCounters::attach(NetworkChannel &channel)
{
  channel.setConnectHandler([this]() { connected++; });
  channel.setDisconnectHandler(
      [this](const boost::system::error_code &) { disconnected++; });
}

} // namespace

SCENARIO(
    "NetworkChannel reports connection loss once per connection", "[Network]")
{
  GIVEN("a server and a client connected on an ephemeral port")
  {
    auto server = std::make_shared<NetworkServer>(0);
    const auto port = server->port();
    REQUIRE(port != 0);
    auto client = std::make_shared<NetworkClient>();
    LifecycleCounters serverCounters;
    LifecycleCounters clientCounters;
    serverCounters.attach(*server);
    clientCounters.attach(*client);

    server->start();
    client->connect(LOOPBACK, port);
    REQUIRE(waitFor([&] {
      return serverCounters.connected == 1 && clientCounters.connected == 1;
    }));
    REQUIRE(serverCounters.disconnected == 0);
    REQUIRE(clientCounters.disconnected == 0);

    WHEN("the client disconnects")
    {
      client->disconnect();

      THEN("both handlers fire exactly once")
      {
        REQUIRE(waitFor([&] { return serverCounters.disconnected == 1; }));
        REQUIRE(clientCounters.disconnected == 1);
        REQUIRE(staysFalse(
            [&] {
              return serverCounters.disconnected != 1
                  || clientCounters.disconnected != 1;
            },
            ERROR_BURST));
        REQUIRE_FALSE(client->isConnected());
      }
    }

    WHEN("the server stops")
    {
      server->stop();

      THEN("the client handler fires exactly once")
      {
        REQUIRE(waitFor([&] { return clientCounters.disconnected == 1; }));
        REQUIRE(serverCounters.disconnected == 1);
        REQUIRE(staysFalse(
            [&] {
              return clientCounters.disconnected != 1
                  || serverCounters.disconnected != 1;
            },
            ERROR_BURST));
      }
    }

    WHEN("a second client connects over the live first one")
    {
      // The replace handler's farewell goes through the ordinary write queue
      // and reaches the first client before the close does, even behind a
      // large write already queued.
      constexpr uint8_t BULK = 41;
      constexpr uint8_t FAREWELL = 42;
      std::atomic<int> bulks{0};
      std::atomic<int> farewells{0};
      std::atomic<int> farewellsBeforeClose{-1};
      client->registerHandler(BULK, [&](const Message &) { bulks++; });
      client->registerHandler(FAREWELL, [&](const Message &) { farewells++; });
      client->setDisconnectHandler([&](const boost::system::error_code &) {
        farewellsBeforeClose = farewells.load();
        clientCounters.disconnected++;
      });
      server->setReplaceHandler([&] {
        const std::vector<uint8_t> bulk(1 << 20, 7);
        server->send(BULK, bulk);
        server->send(FAREWELL);
      });

      auto second = std::make_shared<NetworkClient>();
      LifecycleCounters secondCounters;
      secondCounters.attach(*second);
      second->connect(LOOPBACK, port);

      THEN("the first is told, reported lost, and the second stays connected")
      {
        REQUIRE(waitFor([&] { return serverCounters.connected == 2; }));
        REQUIRE(serverCounters.disconnected == 1);
        REQUIRE(waitFor([&] { return clientCounters.disconnected == 1; }));
        REQUIRE(bulks == 1);
        REQUIRE(farewells == 1);
        REQUIRE(farewellsBeforeClose == 1);
        REQUIRE(waitFor([&] { return secondCounters.connected == 1; }));
        REQUIRE(staysFalse(
            [&] { return secondCounters.disconnected != 0; }, ERROR_BURST));
        REQUIRE(server->isConnected());
        REQUIRE(second->isConnected());

        second->disconnect();
        REQUIRE(waitFor([&] { return serverCounters.disconnected == 2; }));
        REQUIRE(secondCounters.disconnected == 1);
      }
    }

    WHEN("the client reconnects after a closed connection")
    {
      client->disconnect();
      REQUIRE(waitFor([&] { return serverCounters.disconnected == 1; }));

      client->connect(LOOPBACK, port);

      THEN("the connection is re-established and re-armed")
      {
        REQUIRE(waitFor([&] {
          return serverCounters.connected == 2 && clientCounters.connected == 2;
        }));
        REQUIRE(client->isConnected());

        client->disconnect();
        REQUIRE(waitFor([&] { return serverCounters.disconnected == 2; }));
        REQUIRE(clientCounters.disconnected == 2);
      }
    }

    client->disconnect();
    server->stop();
  }

  GIVEN("a client and a host name that cannot resolve")
  {
    auto client = std::make_shared<NetworkClient>();
    LifecycleCounters counters;
    counters.attach(*client);

    WHEN("connect is attempted")
    {
      client->connect("no-such-host.invalid", 1);

      THEN("the failure is reported once through the disconnect handler")
      {
        REQUIRE(waitFor([&] { return counters.disconnected == 1; },
            std::chrono::seconds(30)));
        REQUIRE(counters.connected == 0);
        REQUIRE_FALSE(client->isConnected());
        REQUIRE(staysFalse(
            [&] { return counters.disconnected != 1; }, ERROR_BURST));
      }
    }
  }

  GIVEN("a client and no server listening")
  {
    // Bind and drop an ephemeral port so nothing is listening on it.
    uint16_t port = 0;
    {
      NetworkServer probe(0);
      port = probe.port();
    }
    auto client = std::make_shared<NetworkClient>();
    LifecycleCounters counters;
    counters.attach(*client);

    WHEN("connect is attempted")
    {
      client->connect(LOOPBACK, port);

      THEN("the failure is reported once through the disconnect handler")
      {
        REQUIRE(waitFor([&] { return counters.disconnected == 1; }));
        REQUIRE(counters.connected == 0);
        REQUIRE(staysFalse(
            [&] { return counters.disconnected != 1; }, ERROR_BURST));
      }

      THEN("a disconnect while the attempt is in flight is not confused by it")
      {
        client->disconnect();
        REQUIRE(waitFor([&] { return counters.disconnected == 1; }));
        REQUIRE(staysFalse(
            [&] { return counters.disconnected != 1; }, ERROR_BURST));
        REQUIRE(counters.connected == 0);
      }

      THEN("a later connect to a live server succeeds")
      {
        REQUIRE(waitFor([&] { return counters.disconnected == 1; }));
        auto server = std::make_shared<NetworkServer>(port);
        server->start();
        client->connect(LOOPBACK, port);
        REQUIRE(waitFor([&] { return counters.connected == 1; }));
        REQUIRE(client->isConnected());
        client->disconnect();
        REQUIRE(counters.disconnected == 2);
        server->stop();
      }
    }
  }
}
