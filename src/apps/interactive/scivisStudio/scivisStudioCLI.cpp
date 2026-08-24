// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioCLI.h"

#include "vsr/core/Logging.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, const char **argv)
{
  using namespace vsr::scivis_studio;

  std::vector<std::string> args(argv, argv + argc);
  const auto programName = args.empty() ? "scivisStudioCLI" : args[0];

  StudioCommandLine commandLine;
  std::string error;
  if (!parseStudioCommandLine(args, commandLine, error)) {
    std::cerr << error << "\n\n" << studioCLIUsage(programName);
    return 1;
  }

  if (commandLine.showHelp || commandLine.command == StudioCommand::None) {
    std::cout << studioCLIUsage(programName);
    return commandLine.showHelp ? 0 : 1;
  }

  // stdout carries only command results; all logging, diagnostics, and
  // progress go to stderr.
  vsr::core::setLogToStderr();

  return runStudioCommand(commandLine, std::cin, std::cout);
}
