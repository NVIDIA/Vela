// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <chrono>
#include <functional>
#include <thread>

/*
 * Waiting helpers shared by the tests that drive network endpoints: an IO
 * thread delivers results asynchronously, so a test spins on a predicate with
 * a deadline instead of sleeping for a guessed span.
 *
 * Example:
 *   REQUIRE(waitFor([&] { return counters.connected == 1; }));
 *   REQUIRE(pollUntil(connection, [&] { return bootstraps == 1; }));
 */

// Spins until `done` holds or the deadline passes; false on timeout.
inline bool waitFor(const std::function<bool()> &done,
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

// Drives `pollable.poll()` (a UI-thread pump such as ServerConnection) until
// `done` holds or the deadline passes; false on timeout.
template <typename Pollable>
inline bool pollUntil(Pollable &pollable,
    const std::function<bool()> &done,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pollable.poll();
    if (done())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  pollable.poll();
  return done();
}

// Keeps polling for a fixed span; for asserting that nothing happens.
template <typename Pollable>
inline void pollFor(Pollable &pollable, std::chrono::milliseconds span)
{
  const auto deadline = std::chrono::steady_clock::now() + span;
  while (std::chrono::steady_clock::now() < deadline) {
    pollable.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
