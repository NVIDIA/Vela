// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "NetworkTestHelpers.h"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <chrono>

/*
 * Helpers shared by the Studio remote tests (client core, server, end to
 * end): whether a real ANARI device is available, and connection timings
 * short enough to exercise every liveness timer inside a test.
 *
 * Example:
 *   if (!helideAvailable())
 *     return;
 *   ServerConnection connection(&mirror, fastTimings());
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
