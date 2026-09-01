// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// std
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

using namespace vsr::network;

namespace {

bool waitFor(const std::function<bool()> &done,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}

struct LifecycleCounters
{
  std::atomic<int> connected{0};
  std::atomic<int> disconnected{0};

  void attach(NetworkChannel &channel)
  {
    channel.setConnectHandler([this]() { connected++; });
    channel.setDisconnectHandler(
        [this](const boost::system::error_code &) { disconnected++; });
  }
};

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
    client->connect("127.0.0.1", port);
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
        // Settle: a burst of read/write errors on the same connection must
        // not report again.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(serverCounters.disconnected == 1);
        REQUIRE(clientCounters.disconnected == 1);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(clientCounters.disconnected == 1);
        REQUIRE(serverCounters.disconnected == 1);
      }
    }

    WHEN("the client reconnects after a closed connection")
    {
      client->disconnect();
      REQUIRE(waitFor([&] { return serverCounters.disconnected == 1; }));

      client->connect("127.0.0.1", port);

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

  GIVEN("a client and no server listening")
  {
    // Bind and drop an ephemeral port so nothing is listening on it.
    unsigned short port = 0;
    {
      NetworkServer probe(0);
      port = probe.port();
    }
    auto client = std::make_shared<NetworkClient>();
    LifecycleCounters counters;
    counters.attach(*client);

    WHEN("connect is attempted")
    {
      client->connect("127.0.0.1", port);

      THEN("the failure is reported once through the disconnect handler")
      {
        REQUIRE(waitFor([&] { return counters.disconnected == 1; }));
        REQUIRE(counters.connected == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(counters.disconnected == 1);
      }

      THEN("a later connect to a live server succeeds")
      {
        REQUIRE(waitFor([&] { return counters.disconnected == 1; }));
        auto server = std::make_shared<NetworkServer>(port);
        server->start();
        client->connect("127.0.0.1", port);
        REQUIRE(waitFor([&] { return counters.connected == 1; }));
        REQUIRE(client->isConnected());
        client->disconnect();
        REQUIRE(counters.disconnected == 2);
        server->stop();
      }
    }
  }
}
