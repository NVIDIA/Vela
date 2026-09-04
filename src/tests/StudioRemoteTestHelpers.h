// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "NetworkTestHelpers.h"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

/*
 * Helpers shared by the Studio remote tests (client core, server, end to
 * end, test client): whether a real ANARI device is available, connection
 * timings short enough to exercise every liveness timer inside a test, and a
 * StudioServer running on its own thread for the clients to talk to.
 *
 * Example:
 *   if (!helideAvailable())
 *     return;
 *   RunningServer server(0);
 *   ServerConnection connection(&mirror, fastTimings());
 *   connection.connect("127.0.0.1", server.port());
 */

// The session tests need a real device; absent builds skip rather than fail.
inline bool helideAvailable()
{
  auto library = anari::loadLibrary("helide",
      [](const void *,
          ANARIDevice,
          ANARIObject,
          anari::DataType,
          ANARIStatusSeverity,
          ANARIStatusCode,
          const char *) {});
  if (!library)
    return false;
  anari::unloadLibrary(library);
  return true;
}

// Liveness and retry timings small enough to observe a loss inside a test,
// generous enough not to flake under ctest parallelism. Tests that bootstrap
// a real server stretch the ping and loss timers.
inline vsr::scivis_studio::client::ConnectionTimings fastTimings(
    std::chrono::milliseconds pingAfterQuiet = std::chrono::milliseconds(100),
    std::chrono::milliseconds lossAfterSilence = std::chrono::milliseconds(400),
    std::chrono::milliseconds autoRetryFor = std::chrono::seconds(10))
{
  vsr::scivis_studio::client::ConnectionTimings t;
  t.pingAfterQuiet = pingAfterQuiet;
  t.lossAfterSilence = lossAfterSilence;
  t.retryInitialDelay = std::chrono::milliseconds(50);
  t.retryMaxDelay = std::chrono::milliseconds(200);
  t.autoRetryFor = autoRetryFor;
  return t;
}

// A StudioServer on the helide device, on its own loop thread. Stopping it is
// what a client sees as the server going away: run() tears the listening
// socket down. The studio layer gets one transform node so tests have a
// node to address; a fresh project has none of its own. `beforeRun` gets the
// scene between start() and the loop thread, the last moment the caller may
// touch it, so a test can seed objects the bootstrap will then mirror.
struct RunningServer
{
  explicit RunningServer(uint16_t port,
      const std::function<void(vsr::scene::Scene &)> &beforeRun = {});
  ~RunningServer();

  void stop();
  uint16_t port() const;
  vsr::scene::Scene &scene();
  const vsr::scivis_studio::Project &project();

  std::unique_ptr<vsr::scivis_studio::server::StudioServer> server;
  bool started{false};
  std::string startError;
  size_t transformNode{VSR_INVALID_INDEX};
  std::atomic<bool> finished{false};
  std::thread thread;
};

inline RunningServer::RunningServer(
    uint16_t port, const std::function<void(vsr::scene::Scene &)> &beforeRun)
{
  vsr::scivis_studio::server::ServerOptions options;
  options.port = port;
  options.library = "helide";
  options.dataRoots = {std::filesystem::temp_directory_path()};
  server = std::make_unique<vsr::scivis_studio::server::StudioServer>(options);
  started = server->start(&startError);
  if (!started)
    return;
  // Before run(): start() is the last thing on this thread that may touch the
  // scene.
  auto &s = scene();
  if (auto *layer = s.layer("studio")) {
    auto node = s.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "test transform");
    transformNode = node.index();
  }
  if (beforeRun)
    beforeRun(s);
  thread = std::thread([this] {
    server->run();
    finished.store(true);
  });
}

inline RunningServer::~RunningServer()
{
  stop();
}

inline void RunningServer::stop()
{
  if (!thread.joinable())
    return;
  server->requestShutdown();
  thread.join();
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
