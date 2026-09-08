// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
// posix
#include <sys/types.h>

namespace vsr::scivis_studio::test_client {

/*
 * A scivisStudioServer the test client spawned and owns. The first start asks
 * the OS for a free port (`--port 0`) and reads the one bound off the
 * server's `Listening on port N` line; every later start binds that same
 * port again, so a client's `reconnect` finds the replacement where the
 * original was. The server's stdout and stderr go to one log file for the
 * whole run, every lifetime appended, so a failed scenario's post-mortem sees
 * them all. `--port` and `--data-root` are the process's to append: the
 * command names the binary and the server's other arguments.
 *
 * kill() is SIGKILL, the "process went away" case the loss scenarios script;
 * stop() is SIGTERM with a SIGKILL after a grace period, for the end of a run.
 * Nothing here waits unboundedly: awaitListening() takes a deadline and the
 * grace period is fixed.
 *
 * Example:
 *   ServerProcess server({serverBinary, "--library", "helide"},
 *       work / "data", work / "server.log");
 *   std::string error;
 *   if (!server.start(&error))
 *     return fail(error);
 *   if (server.awaitListening(30s, &error) != ServerProcess::Start::Listening)
 *     return fail(error);
 *   options.port = server.port();
 *   ...
 *   server.stop();
 */
struct ServerProcess
{
  // How a start went: the server listens; it exited saying no ANARI device
  // could be loaded (a skip, not a failure); it exited otherwise or never
  // reached Listening; it is still starting (check() only).
  enum class Start
  {
    Listening,
    NoDevice,
    Failed,
    Starting
  };

  ServerProcess(std::vector<std::string> command,
      std::filesystem::path dataRoot,
      std::filesystem::path logPath);
  // stop()s a server still running.
  ~ServerProcess();

  VSR_NOT_COPYABLE(ServerProcess)

  // Spawns the server. False with the reason when the spawn itself fails (no
  // such binary, say) or one is already running; whether the server then
  // comes up is awaitListening()'s to tell.
  bool start(std::string *error = nullptr);
  // Where the newest start stands, without waiting: Listening once its
  // Listening line is in the log (port() is then known), NoDevice or Failed
  // once it has exited, Starting meanwhile. `error` gets Failed's reason.
  Start check(std::string *error = nullptr);
  // check() until it is not Starting or the deadline passes (then Failed).
  Start awaitListening(
      std::chrono::milliseconds deadline, std::string *error = nullptr);
  // Whether the process is still there.
  bool running();
  // SIGKILL and reap: the server goes away without a farewell.
  void kill();
  // SIGTERM, a grace period, then SIGKILL if it is still there; reaped.
  void stop();

  // The port the server bound, 0 until the first start reached Listening.
  uint16_t port() const;
  const std::filesystem::path &dataRoot() const;
  const std::filesystem::path &logPath() const;
  // The log file's whole text.
  std::string log() const;

 private:
  // The log from where the newest start began writing.
  std::string logSinceStart() const;
  // Reaps an exited child; true when it is gone.
  bool reap(bool block);

  std::vector<std::string> m_command;
  std::filesystem::path m_dataRoot;
  std::filesystem::path m_logPath;
  pid_t m_pid{0};
  int m_exitStatus{0};
  uint16_t m_port{0};
  bool m_listening{false};
  // Where the log stood when the newest start began.
  std::uintmax_t m_logStart{0};
};

// Makes this process take its spawned server with it when it dies. Installs
// SIGTERM/SIGINT handlers that SIGTERM the running ServerProcess's child
// before re-raising the signal with the default disposition, and a
// std::terminate hook that does the same for an uncaught exception. Without
// it only ~ServerProcess() stops the server, so a signalled or terminating
// client leaves it re-parented to init. SIGSEGV and SIGKILL stay uncovered.
// Called from main(); calling it again does nothing. The handler uses only
// kill(), signal() and raise().
void stopSpawnedServerOnDeath();

} // namespace vsr::scivis_studio::test_client
