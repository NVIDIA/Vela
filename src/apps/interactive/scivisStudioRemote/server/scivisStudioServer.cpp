// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

vsr::scivis_studio::server::StudioServer *g_server{nullptr};

std::atomic<bool> g_shutdownSignalled{false};

// Only async-signal-safe calls here. The first signal asks for a clean
// shutdown, which the loop notices once the running Server Task returns: an
// import is not cancellable and can take minutes. A second signal restores
// the default disposition and re-raises it, ending the process at once.
void onSignal(int sig)
{
  if (g_shutdownSignalled.exchange(true)) {
    std::signal(sig, SIG_DFL);
    std::raise(sig);
    return;
  }
#ifndef _WIN32
  static constexpr char message[] =
      "[StudioServer] shutdown requested; waiting for the running task "
      "(signal again to exit immediately)\n";
  [[maybe_unused]] const auto written =
      ::write(STDOUT_FILENO, message, sizeof(message) - 1);
#endif
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

  // Without a logging callback every status and error line is dropped. Line
  // buffering keeps the lines flowing when stdout is a pipe or a file, so a
  // launcher can wait for "Listening on port" instead of probing the socket.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
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
