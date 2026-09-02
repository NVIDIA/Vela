// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandRunner.h"
#include "Script.h"
#include "TestClientOptions.h"
#include "TestSession.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace vsr::scivis_studio::test_client;

// The script text and the line its first command sits on.
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
    return true;
  }
  if (!options.inlineScripts.empty()) {
    // Each -e is one script line, so records name it by its ordinal.
    for (const auto &piece : options.inlineScripts)
      script += piece + '\n';
    return true;
  }
  script.assign(std::istreambuf_iterator<char>(std::cin), {});
  return true;
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

  // stdout is the record stream alone; the transport and session logs go to
  // stderr.
  vsr::core::setLogToStderr();

  TestSession session;
  CommandRunner runner(&session, &std::cout, options.runner);
  const bool ok = runner.run(commands);
  session.disconnect();
  return ok ? 0 : 1;
}
