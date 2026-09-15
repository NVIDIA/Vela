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
 *   REQUIRE(staysFalse([&] { return counters.disconnected > 1; }, 50ms));
 */

// Every test endpoint listens on the loopback interface.
inline constexpr const char *LOOPBACK = "127.0.0.1";

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

// Keeps checking `changed` for the whole span; true if it never held. For
// asserting that nothing happens (a second report, a frame while paused)
// without sleeping blind: the first change fails the wait at once.
inline bool staysFalse(
    const std::function<bool()> &changed, std::chrono::milliseconds span)
{
  const auto deadline = std::chrono::steady_clock::now() + span;
  while (std::chrono::steady_clock::now() < deadline) {
    if (changed())
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return !changed();
}

// Waits out a span the test cannot watch a predicate over: choreography
// against another thread whose progress is not observable. Prefer
// staysFalse() wherever the thing that must not happen can be named.
inline void settle(std::chrono::milliseconds span)
{
  std::this_thread::sleep_for(span);
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
