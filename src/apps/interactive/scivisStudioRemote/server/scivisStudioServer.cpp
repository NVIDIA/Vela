// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

namespace {

vsr::scivis_studio::server::StudioServer *g_server{nullptr};

// Only an atomic store happens here; the loop notices at its next iteration.
void onSignal(int)
{
  if (g_server)
    g_server->requestShutdown();
}

} // namespace

int main(int argc, const char **argv)
{
  using namespace vsr::scivis_studio::server;

  std::vector<std::string> args(argv, argv + argc);
  const auto programName = args.empty() ? "scivisStudioServer" : args[0];

  ServerOptions options;
  std::string error;
  if (!parseServerOptions(args, options, &error)) {
    std::cerr << error << '\n' << serverUsage(programName);
    return 1;
  }
  if (options.showHelp) {
    std::cout << serverUsage(programName);
    return 0;
  }

  // Without a logging callback every status and error line is dropped.
  vsr::core::setLogToStdout();

  StudioServer server(options);
  if (!server.start(&error)) {
    vsr::core::logError("[StudioServer] %s", error.c_str());
    return 1;
  }

  g_server = &server;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  server.run();

  g_server = nullptr;
  return 0;
}
