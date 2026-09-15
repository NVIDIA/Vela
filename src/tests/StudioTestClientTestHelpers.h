// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// catch
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "Script.h"
#include "TestSession.h"
// std
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

/*
 * Helpers for the suites that drive the test client's script runner: run a
 * script on a TestSession and read the record stream it printed, line by
 * line.
 *
 * Example:
 *   TestSession session;
 *   const auto result = runScript(session, "connect 127.0.0.1 4242\nping\n");
 *   REQUIRE(result.ok);
 *   REQUIRE(hasLine(result.records, "OK ping"));
 */

// Bounds every waiting command a test script runs.
inline constexpr std::chrono::seconds TEST_TIMEOUT{10};

inline std::vector<std::string> lines(const std::string &text)
{
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    out.push_back(line);
  return out;
}

inline bool hasLine(
    const std::vector<std::string> &records, const std::string &exact)
{
  for (const auto &r : records)
    if (r == exact)
      return true;
  return false;
}

inline bool hasLineStarting(
    const std::vector<std::string> &records, const std::string &prefix)
{
  for (const auto &r : records)
    if (r.rfind(prefix, 0) == 0)
      return true;
  return false;
}

inline size_t countStarting(
    const std::vector<std::string> &records, const std::string &prefix)
{
  size_t n = 0;
  for (const auto &r : records)
    n += r.rfind(prefix, 0) == 0;
  return n;
}

inline std::vector<std::string> failLines(
    const std::vector<std::string> &records)
{
  std::vector<std::string> out;
  for (const auto &r : records)
    if (r.rfind("FAIL ", 0) == 0)
      out.push_back(r);
  return out;
}

// Options that carry a script on past its FAILs.
inline vsr::scivis_studio::test_client::RunnerOptions keepGoing()
{
  vsr::scivis_studio::test_client::RunnerOptions options;
  options.keepGoing = true;
  return options;
}

// run()'s verdict on a script and the record stream it printed.
struct RunResult
{
  bool ok{false};
  std::vector<std::string> records;
};

// Parses `script` and runs it through a fresh runner on `session`.
inline RunResult runScript(
    vsr::scivis_studio::test_client::TestSession &session,
    const std::string &script,
    vsr::scivis_studio::test_client::RunnerOptions options = {})
{
  using namespace vsr::scivis_studio::test_client;

  std::vector<Command> commands;
  std::string error;
  REQUIRE(parseScript(script, commands, &error));
  std::ostringstream out;
  options.timeout = TEST_TIMEOUT;
  CommandRunner runner(&session, &out, options);
  RunResult result;
  result.ok = runner.run(commands);
  result.records = lines(out.str());
  return result;
}
