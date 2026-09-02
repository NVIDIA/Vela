// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "NetworkTestHelpers.h"
// vsr_scivis_studio_server_core
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "ProjectOpReply.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

/*
 * Helpers for the tests that drive an in-process StudioServer over a raw
 * NetworkClient: a client that records every Studio message in arrival
 * order, and a loop guard that runs the server on its own thread and always
 * brings it down.
 *
 * Example:
 *   ServerLoop loop(&server);
 *   TestClient client;
 *   client.connect(server.port());
 *   REQUIRE(client.waitForCount(StudioMessageType::Hello, 1));
 *   client.send(Hello{});
 *   REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1));
 */

// A raw NetworkClient that records every Studio message in arrival order.
struct TestClient
{
  using Message = vsr::network::Message;
  using StudioMessageType = vsr::scivis_studio::protocol::StudioMessageType;

  TestClient();
  ~TestClient();

  void connect(unsigned short port);
  template <typename T>
  void send(const T &payload);

  size_t count(StudioMessageType type);
  bool waitForCount(StudioMessageType type, size_t n);
  std::vector<Message> messages();
  // The last message of a type, or an invalid Message.
  Message last(StudioMessageType type);
  // The last decodable T of its type, if any arrived.
  template <typename T>
  std::optional<T> lastDecoded();
  // The ProjectOpReply answering `requestId`, waiting for it to arrive.
  std::optional<vsr::scivis_studio::protocol::ProjectOpReply> waitForReply(
      uint64_t requestId,
      std::chrono::milliseconds timeout = std::chrono::seconds(5));
  // Position of the first recorded message of a type, or SIZE_MAX.
  size_t indexOf(StudioMessageType type);
  void clear();

  std::shared_ptr<vsr::network::NetworkClient> channel;
  std::mutex mutex;
  std::vector<Message> received;
  std::atomic<int> disconnects{0};
};

// Runs the server loop on its own thread and always brings it down, so a
// failed REQUIRE never leaves a joinable thread behind.
struct ServerLoop
{
  explicit ServerLoop(vsr::scivis_studio::server::StudioServer *server);
  ~ServerLoop();

  vsr::scivis_studio::server::StudioServer *server{nullptr};
  std::atomic<bool> finished{false};
  std::thread thread;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline TestClient::TestClient()
{
  channel = std::make_shared<vsr::network::NetworkClient>();
  for (int value = 1; value < vsr::network::MESSAGE_TYPE_INVALID; ++value) {
    if (!vsr::scivis_studio::protocol::isStudioMessageType(uint8_t(value)))
      continue;
    channel->registerHandler(uint8_t(value), [this](const Message &msg) {
      std::lock_guard lock(mutex);
      received.push_back(msg);
    });
  }
  channel->setDisconnectHandler(
      [this](const boost::system::error_code &) { disconnects++; });
}

inline TestClient::~TestClient()
{
  channel->disconnect();
}

inline void TestClient::connect(unsigned short port)
{
  channel->connect("127.0.0.1", short(port));
}

template <typename T>
inline void TestClient::send(const T &payload)
{
  channel->send(vsr::scivis_studio::protocol::encode(payload));
}

inline size_t TestClient::count(StudioMessageType type)
{
  std::lock_guard lock(mutex);
  size_t n = 0;
  for (const auto &msg : received)
    n += msg.header.type == uint8_t(type);
  return n;
}

inline bool TestClient::waitForCount(StudioMessageType type, size_t n)
{
  return waitFor([&] { return count(type) >= n; });
}

inline std::vector<TestClient::Message> TestClient::messages()
{
  std::lock_guard lock(mutex);
  return received;
}

inline TestClient::Message TestClient::last(StudioMessageType type)
{
  std::lock_guard lock(mutex);
  for (auto it = received.rbegin(); it != received.rend(); ++it)
    if (it->header.type == uint8_t(type))
      return *it;
  return {};
}

template <typename T>
inline std::optional<T> TestClient::lastDecoded()
{
  const auto msg = last(T::MESSAGE_TYPE);
  if (msg.header.type != uint8_t(T::MESSAGE_TYPE))
    return {};
  return vsr::scivis_studio::protocol::decode<T>(msg);
}

inline std::optional<vsr::scivis_studio::protocol::ProjectOpReply>
TestClient::waitForReply(uint64_t requestId, std::chrono::milliseconds timeout)
{
  using vsr::scivis_studio::protocol::ProjectOpReply;
  std::optional<ProjectOpReply> found;
  waitFor(
      [&] {
        std::lock_guard lock(mutex);
        for (const auto &msg : received) {
          if (msg.header.type != uint8_t(StudioMessageType::ProjectOpReply))
            continue;
          auto reply =
              vsr::scivis_studio::protocol::decode<ProjectOpReply>(msg);
          if (reply && reply->requestId == requestId) {
            found = std::move(reply);
            return true;
          }
        }
        return false;
      },
      timeout);
  return found;
}

inline size_t TestClient::indexOf(StudioMessageType type)
{
  std::lock_guard lock(mutex);
  for (size_t i = 0; i < received.size(); ++i)
    if (received[i].header.type == uint8_t(type))
      return i;
  return SIZE_MAX;
}

inline void TestClient::clear()
{
  std::lock_guard lock(mutex);
  received.clear();
}

inline ServerLoop::ServerLoop(vsr::scivis_studio::server::StudioServer *server)
    : server(server), thread([this] {
        this->server->run();
        finished.store(true);
      })
{}

inline ServerLoop::~ServerLoop()
{
  server->requestShutdown();
  thread.join();
}
