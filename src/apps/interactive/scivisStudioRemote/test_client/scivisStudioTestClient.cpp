// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandRunner.h"
#include "Script.h"
#include "ServerProcess.h"
#include "TestClientOptions.h"
#include "TestSession.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

// What the spawned server gets to reach Listening, first start and restarts
// alike; a script's await-server has its own deadline.
constexpr auto SERVER_START_TIMEOUT = 30s;
// ctest's SKIP_RETURN_CODE for the scenarios.
constexpr int EXIT_SKIP = 77;

// The script text: the --script file, the -e pieces one per line, or stdin.
bool readScript(
    const TestClientOptions &options, std::string &script, std::string &error)
{
  if (!options.scriptPath.empty()) {
    std::ifstream file(options.scriptPath);
    if (!file) {
      error = "cannot read script " + options.scriptPath;
      return false;
    }
    script.assign(std::istreambuf_iterator<char>(file), {});
    // A directory opens but does not read.
    if (file.bad()) {
      error = "cannot read script " + options.scriptPath;
      return false;
    }
    return true;
  }
  if (!options.inlineScripts.empty()) {
    for (const auto &piece : options.inlineScripts)
      script += piece + '\n';
    return true;
  }
  script.assign(std::istreambuf_iterator<char>(std::cin), {});
  return true;
}

// A fresh directory under the system temp dir, named after the script.
bool makeWorkDir(const std::string &scriptPath,
    std::filesystem::path &work,
    std::string &error)
{
  const auto name = scriptPath.empty()
      ? std::string("inline")
      : std::filesystem::path(scriptPath).stem().string();
  auto pattern =
      (std::filesystem::temp_directory_path() / ("vsrStudioScenario-" + name))
          .string()
      + "-XXXXXX";
  if (!::mkdtemp(pattern.data())) {
    error = "cannot create a working directory from " + pattern;
    return false;
  }
  work = pattern;
  return true;
}

// The spawned server's whole log, for a post-mortem.
void dumpServerLog(const ServerProcess &server)
{
  std::cerr << "==== server log (" << server.logPath().string() << ")\n"
            << server.log() << "====\n";
}

// A failed run's epilogue: the server log, and the work dir left in place.
void keepWorkDir(const ServerProcess &server, const std::filesystem::path &work)
{
  dumpServerLog(server);
  std::cerr << "[scivisStudioTestClient] work dir kept: " << work.string()
            << '\n';
}

} // namespace

int main(int argc, const char **argv)
{
  std::vector<std::string> args(argv, argv + argc);
  const auto programName = args.empty() ? "scivisStudioTestClient" : args[0];

  TestClientOptions options;
  std::string error;
  if (!parseTestClientOptions(args, options, &error)) {
    std::cerr << error << '\n' << testClientUsage(programName);
    return 2;
  }
  if (options.showHelp) {
    std::cout << testClientUsage(programName);
    return 0;
  }
  if (options.showMarkdown) {
    std::cout << testClientCommandTable() << '\n'
              << testClientAssertValueTable();
    return 0;
  }

  // Everything relative on the command line is resolved before a spawned
  // server's working directory becomes the current one.
  std::string script;
  if (!readScript(options, script, error)) {
    std::cerr << error << '\n';
    return 2;
  }
  std::vector<Command> commands;
  if (!parseScript(script, commands, &error)) {
    std::cerr << "script: " << error << '\n';
    return 2;
  }
  // An empty, comment-only or misnamed script must not pass by running
  // nothing.
  if (commands.empty()) {
    std::cerr << "script: no commands to run\n";
    return 2;
  }
  options.runner.scriptDir = options.scriptPath.empty()
      ? std::filesystem::current_path()
      : std::filesystem::absolute(options.scriptPath).parent_path();

  // stdout is the record stream alone; the transport and session logs go to
  // stderr.
  vsr::core::setLogToStderr();

  // The spawned server, if any: a fresh working directory holding its data
  // root and log, the server on a port of the OS's choosing, and the script
  // run from that directory so relative save-frame paths land there.
  std::unique_ptr<ServerProcess> server;
  std::filesystem::path work;
  if (!options.spawnServer.empty()) {
    if (!makeWorkDir(options.scriptPath, work, error)) {
      std::cerr << error << '\n';
      return 2;
    }
    auto command = options.spawnServer;
    // A path stays valid from the work dir; a bare name is PATH's to find.
    if (command[0].find('/') != std::string::npos)
      command[0] = std::filesystem::absolute(command[0]).string();
    std::filesystem::create_directories(work / "data");
    server = std::make_unique<ServerProcess>(
        command, work / "data", work / "server.log");
    std::filesystem::current_path(work);
    std::cerr << "[scivisStudioTestClient] work dir " << work.string() << '\n';
    if (!server->start(&error)) {
      std::cerr << error << '\n';
      std::filesystem::remove_all(work);
      return 2;
    }
    const auto started = server->awaitListening(SERVER_START_TIMEOUT, &error);
    if (started == ServerProcess::Start::NoDevice) {
      std::cerr << "[scivisStudioTestClient] the server loaded no ANARI"
                   " device"
                << (options.requireDevice ? "; skipping" : "") << '\n';
      dumpServerLog(*server);
      std::filesystem::remove_all(work);
      return options.requireDevice ? EXIT_SKIP : 1;
    }
    if (started != ServerProcess::Start::Listening) {
      std::cerr << error << '\n';
      keepWorkDir(*server, work);
      return 1;
    }
    options.runner.port = server->port();
  }

  TestSession session;
  CommandRunner runner(&session, &std::cout, options.runner, server.get());
  const bool ok = runner.run(commands);
  session.disconnect();

  if (server) {
    server->stop();
    if (ok)
      std::filesystem::remove_all(work);
    else
      keepWorkDir(*server, work);
  }
  return ok ? 0 : 1;
}
