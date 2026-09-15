// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tests
#include "NetworkTestHelpers.h"
#include "StudioServerTestHelpers.h"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/*
 * Helpers shared by the Studio remote tests (client core, server, end to
 * end, test client): whether a real ANARI device is available, connection
 * timings short enough to exercise every liveness timer inside a test, the
 * options of the RunningServer the client-core suites talk to, and the
 * client core on a mirror Scene those suites drive.
 *
 * Example:
 *   if (!helideAvailable())
 *     return;
 *   RunningServer server(tempRootServerOptions());
 *   MirroredClient client(fastTimings(200ms, 2s, 30s), 10s);
 *   client.connect(server.port());
 *   REQUIRE(client.waitConnectedAndBootstrapped());
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

// Options for the server the client-core suites talk to: the temp directory
// is its Data Root, since their scratch directories live there. A nonzero
// `port` restarts a server where a client last saw one.
inline vsr::scivis_studio::server::ServerOptions tempRootServerOptions(
    uint16_t port = 0);

// The client core on a mirror Scene, counting bootstraps and collecting the
// server's errors, with the wait every session test opens on. Fixtures that
// need more (a fake server, a structure counter) derive from it. `timeout`
// bounds the bootstrap wait; a real server's is longer than a fake one's.
struct MirroredClient
{
  explicit MirroredClient(
      const vsr::scivis_studio::client::ConnectionTimings &timings =
          fastTimings(),
      std::chrono::milliseconds timeout = std::chrono::seconds(5));

  void connect(uint16_t port);
  // Polls until the connection is Connected and `expectedBootstraps` have
  // completed; false on timeout.
  bool waitConnectedAndBootstrapped(int expectedBootstraps = 1);

  vsr::scene::Scene mirror;
  vsr::scivis_studio::client::ServerConnection connection;
  int bootstraps{0};
  std::vector<std::string> errors;
  std::chrono::milliseconds timeout;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline vsr::scivis_studio::server::ServerOptions tempRootServerOptions(
    uint16_t port)
{
  auto options = testServerOptions({std::filesystem::temp_directory_path()});
  options.port = port;
  return options;
}

inline MirroredClient::MirroredClient(
    const vsr::scivis_studio::client::ConnectionTimings &timings,
    std::chrono::milliseconds timeout)
    : connection(&mirror, timings), timeout(timeout)
{
  connection.onBootstrapComplete = [this]() { bootstraps++; };
  connection.onServerError = [this](
                                 const std::string &m) { errors.push_back(m); };
}

inline void MirroredClient::connect(uint16_t port)
{
  connection.connect(LOOPBACK, port);
}

inline bool MirroredClient::waitConnectedAndBootstrapped(int expectedBootstraps)
{
  using vsr::scivis_studio::client::ConnectionState;
  return pollUntil(
      connection,
      [&] {
        return connection.state() == ConnectionState::Connected
            && bootstraps == expectedBootstraps;
      },
      timeout);
}
