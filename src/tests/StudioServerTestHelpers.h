// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tests
#include "NetworkTestHelpers.h"
// catch
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "ProjectOpReply.h"
#include "ProjectSnapshot.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/*
 * Helpers for the tests that drive an in-process StudioServer over a raw
 * NetworkClient: a client that records every Studio message in arrival
 * order, a loop guard that runs the server on its own thread and always
 * brings it down, a RunningServer that starts the helide server the suites
 * share, and a ServerSession that adds one bootstrapped TestClient with the
 * request/task helpers every project-op scenario opens with.
 *
 * Example:
 *   ServerSession session(testServerOptions({data.root}));
 *   const auto taskId = startedTaskId(session.request(import));
 *   const auto end = session.waitForTaskEnd(taskId);
 *   REQUIRE(end && end->completed);
 */

// How a task ended: its completion message or its error, and the frames a
// render got through (0 for anything else).
struct TaskEnd
{
  bool completed{false};
  std::string text;
  uint64_t framesCompleted{0};
};

// A raw NetworkClient that records every Studio message in arrival order.
struct TestClient
{
  using Message = vsr::network::Message;
  using StudioMessageType = vsr::scivis_studio::protocol::StudioMessageType;

  TestClient();
  ~TestClient();

  void connect(uint16_t port);
  template <typename T>
  void send(const T &payload);

  size_t count(StudioMessageType type);
  bool waitForCount(StudioMessageType type,
      size_t n,
      std::chrono::milliseconds timeout = std::chrono::seconds(5));
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
  // How task `taskId` ended, waiting for its TaskCompleted or TaskFailed.
  std::optional<TaskEnd> waitForTaskEnd(uint64_t taskId,
      std::chrono::milliseconds timeout = std::chrono::seconds(5));
  // Position of the first recorded message of a type, or SIZE_MAX.
  size_t indexOf(StudioMessageType type);
  // Position of the ProjectOpReply answering `requestId`, or SIZE_MAX.
  size_t indexOfReply(uint64_t requestId);
  // Position of the TaskCompleted for `taskId` at or after `from`, or
  // SIZE_MAX.
  size_t indexOfCompletedFrom(uint64_t taskId, size_t from);
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

// Options for an in-process test server: the helide device, a port the OS
// picks, and the given Data Roots (none by default).
inline vsr::scivis_studio::server::ServerOptions testServerOptions(
    std::vector<std::filesystem::path> dataRoots = {});

// A StudioServer started on `options` and run on its own loop thread.
// Stopping it is what a client sees as the server going away: run() tears
// the listening socket down. `beforeLoop` runs between start() and the loop
// thread, the last moment the caller may touch the scene or the project, so
// a test can seed objects the bootstrap will then mirror. Never REQUIREs
// (`started` and `startError` say how start() went), so a restart may build
// one off the test thread.
struct RunningServer
{
  explicit RunningServer(
      vsr::scivis_studio::server::ServerOptions options = testServerOptions(),
      const std::function<void(vsr::scivis_studio::server::StudioServer &)>
          &beforeLoop = {});

  // Ends run() and joins the loop thread; what a client sees as the server
  // going away.
  void stop();
  // Whether run() has returned (a Shutdown arrived, or stop() was called).
  bool finished() const;
  uint16_t port() const;
  vsr::scene::Scene &scene();
  const vsr::scivis_studio::Project &project();

  vsr::scivis_studio::server::ServerOptions options;
  std::unique_ptr<vsr::scivis_studio::server::StudioServer> server;
  bool started{false};
  std::string startError;
  std::unique_ptr<ServerLoop> loop;
};

// Connects `client` and runs its Hello/bootstrap handshake, leaving the
// bootstrap's messages recorded.
inline void bootstrapClient(TestClient &client,
    uint16_t port,
    std::chrono::milliseconds timeout = std::chrono::seconds(5));

// A RunningServer with one TestClient through the handshake and the server
// Established; the bootstrap's messages stay recorded until the caller
// clears them. `timeout` bounds every wait the session makes. REQUIREs, so
// build it on the test thread.
struct ServerSession : RunningServer
{
  explicit ServerSession(
      vsr::scivis_studio::server::ServerOptions options = testServerOptions(),
      const std::function<void(vsr::scivis_studio::server::StudioServer &)>
          &beforeLoop = {},
      std::chrono::milliseconds timeout = std::chrono::seconds(5));

  // Sends `req` with a fresh requestId and waits for its reply.
  template <typename R>
  vsr::scivis_studio::protocol::ProjectOpReply request(R req);
  std::optional<TaskEnd> waitForTaskEnd(uint64_t taskId);
  // Waits until `n` snapshots have arrived since the last clear().
  bool waitForSnapshots(size_t n);
  vsr::scivis_studio::protocol::ProjectSnapshot latestSnapshot();

  TestClient client;
  uint64_t nextRequestId{1};
  std::chrono::milliseconds timeout;
};

// The task id a TaskStarted reply names; fails the test on any other reply.
inline uint64_t startedTaskId(
    const vsr::scivis_studio::protocol::ProjectOpReply &reply);

// Writes the one-triangle OBJ (corners at the origin, (1, 0, 0) and
// (0, 1, 0) in the z = 0 plane) the server suites import.
inline void writeTriangleObj(const std::filesystem::path &file);

// Inlined definitions ////////////////////////////////////////////////////////

inline vsr::scivis_studio::server::ServerOptions testServerOptions(
    std::vector<std::filesystem::path> dataRoots)
{
  vsr::scivis_studio::server::ServerOptions options;
  options.port = 0;
  options.library = "helide";
  options.dataRoots = std::move(dataRoots);
  return options;
}

inline void bootstrapClient(
    TestClient &client, uint16_t port, std::chrono::milliseconds timeout)
{
  using vsr::scivis_studio::protocol::Hello;
  using vsr::scivis_studio::protocol::StudioMessageType;
  client.connect(port);
  REQUIRE(client.waitForCount(StudioMessageType::Hello, 1, timeout));
  client.send(Hello{});
  REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1, timeout));
}

inline uint64_t startedTaskId(
    const vsr::scivis_studio::protocol::ProjectOpReply &reply)
{
  using vsr::scivis_studio::protocol::TaskStartedResult;
  REQUIRE(reply.ok);
  const auto started =
      vsr::scivis_studio::protocol::results<TaskStartedResult>(reply);
  REQUIRE(started);
  REQUIRE(started->taskId != 0);
  return started->taskId;
}

inline void writeTriangleObj(const std::filesystem::path &file)
{
  std::ofstream(file) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
}

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

inline void TestClient::connect(uint16_t port)
{
  channel->connect(LOOPBACK, port);
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

inline bool TestClient::waitForCount(
    StudioMessageType type, size_t n, std::chrono::milliseconds timeout)
{
  return waitFor([&] { return count(type) >= n; }, timeout);
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

inline std::optional<TaskEnd> TestClient::waitForTaskEnd(
    uint64_t taskId, std::chrono::milliseconds timeout)
{
  using namespace vsr::scivis_studio::protocol;
  std::optional<TaskEnd> end;
  waitFor(
      [&] {
        for (const auto &msg : messages()) {
          if (auto completed = decode<TaskCompleted>(msg);
              completed && completed->taskId == taskId) {
            end = TaskEnd{
                true, completed->message, framesCompletedOf(*completed)};
            return true;
          }
          if (auto failed = decode<TaskFailed>(msg);
              failed && failed->taskId == taskId) {
            end = TaskEnd{false, failed->error, framesCompletedOf(*failed)};
            return true;
          }
        }
        return false;
      },
      timeout);
  return end;
}

inline size_t TestClient::indexOf(StudioMessageType type)
{
  std::lock_guard lock(mutex);
  for (size_t i = 0; i < received.size(); ++i)
    if (received[i].header.type == uint8_t(type))
      return i;
  return SIZE_MAX;
}

inline size_t TestClient::indexOfReply(uint64_t requestId)
{
  using vsr::scivis_studio::protocol::ProjectOpReply;
  std::lock_guard lock(mutex);
  for (size_t i = 0; i < received.size(); ++i) {
    if (received[i].header.type != uint8_t(StudioMessageType::ProjectOpReply))
      continue;
    const auto reply =
        vsr::scivis_studio::protocol::decode<ProjectOpReply>(received[i]);
    if (reply && reply->requestId == requestId)
      return i;
  }
  return SIZE_MAX;
}

inline size_t TestClient::indexOfCompletedFrom(uint64_t taskId, size_t from)
{
  using vsr::scivis_studio::protocol::TaskCompleted;
  std::lock_guard lock(mutex);
  for (size_t i = from; i < received.size(); ++i) {
    if (auto completed =
            vsr::scivis_studio::protocol::decode<TaskCompleted>(received[i]);
        completed && completed->taskId == taskId)
      return i;
  }
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

inline RunningServer::RunningServer(
    vsr::scivis_studio::server::ServerOptions options,
    const std::function<void(vsr::scivis_studio::server::StudioServer &)>
        &beforeLoop)
    : options(std::move(options))
{
  server =
      std::make_unique<vsr::scivis_studio::server::StudioServer>(this->options);
  started = server->start(&startError);
  if (!started)
    return;
  if (beforeLoop)
    beforeLoop(*server);
  loop = std::make_unique<ServerLoop>(server.get());
}

inline void RunningServer::stop()
{
  loop.reset();
}

inline bool RunningServer::finished() const
{
  return started && (!loop || loop->finished.load());
}

inline uint16_t RunningServer::port() const
{
  return server->port();
}

inline vsr::scene::Scene &RunningServer::scene()
{
  return server->appContext().vsr.scene;
}

inline const vsr::scivis_studio::Project &RunningServer::project()
{
  return server->projectContext().project();
}

inline ServerSession::ServerSession(
    vsr::scivis_studio::server::ServerOptions options,
    const std::function<void(vsr::scivis_studio::server::StudioServer &)>
        &beforeLoop,
    std::chrono::milliseconds timeout)
    : RunningServer(std::move(options), beforeLoop), timeout(timeout)
{
  using vsr::scivis_studio::server::SessionState;
  INFO(startError);
  REQUIRE(started);
  bootstrapClient(client, port(), timeout);
  REQUIRE(waitFor(
      [&] { return server->sessionState() == SessionState::Established; },
      timeout));
}

template <typename R>
inline vsr::scivis_studio::protocol::ProjectOpReply ServerSession::request(
    R req)
{
  req.requestId = nextRequestId++;
  client.send(req);
  const auto reply = client.waitForReply(req.requestId, timeout);
  REQUIRE(reply);
  return *reply;
}

inline std::optional<TaskEnd> ServerSession::waitForTaskEnd(uint64_t taskId)
{
  return client.waitForTaskEnd(taskId, timeout);
}

inline bool ServerSession::waitForSnapshots(size_t n)
{
  return client.waitForCount(
      TestClient::StudioMessageType::ProjectSnapshot, n, timeout);
}

inline vsr::scivis_studio::protocol::ProjectSnapshot
ServerSession::latestSnapshot()
{
  const auto snapshot =
      client.lastDecoded<vsr::scivis_studio::protocol::ProjectSnapshot>();
  REQUIRE(snapshot);
  return *snapshot;
}
