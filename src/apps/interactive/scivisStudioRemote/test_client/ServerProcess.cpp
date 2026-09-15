// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerProcess.h"
// vsr_scivis_studio_protocol
#include "StudioProtocol.h"
// std
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <thread>
// posix
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

namespace vsr::scivis_studio::test_client {

namespace {

using namespace std::chrono_literals;

// The lines the server prints, as the shell runner used to grep them.
constexpr const char *LISTENING = "Listening on port ";
constexpr const char *NO_DEVICE = "no ANARI device could be loaded";
constexpr auto POLL_INTERVAL = 20ms;
constexpr auto STOP_GRACE = 5s;

// The running server's pid, for the death handlers below: a signal handler
// cannot walk a ServerProcess, so start() publishes the pid here and reap()
// clears it. Only those two write it, both on the client's main thread, but
// the signal may be delivered on the transport's I/O thread, so the handler's
// read is a cross-thread one: sig_atomic_t is the type that read is defined
// for. One live server is assumed, the client's; a second ServerProcess
// would overwrite the pid this holds.
volatile std::sig_atomic_t g_spawnedServerPid = 0;
std::terminate_handler g_previousTerminate = nullptr;

// Async-signal-safe: kill(), signal() and raise() only.
void stopSpawnedServer()
{
  const auto pid = g_spawnedServerPid;
  if (pid != 0)
    ::kill(static_cast<pid_t>(pid), SIGTERM);
}

extern "C" void onDeathSignal(int signum)
{
  // kill() may set errno, and a handler owes the interrupted code the errno
  // it left behind.
  const int savedErrno = errno;
  stopSpawnedServer();
  errno = savedErrno;
  // Re-raise so the exit status still says which signal ended the client.
  ::signal(signum, SIG_DFL);
  ::raise(signum);
}

void onTerminate()
{
  stopSpawnedServer();
  if (g_previousTerminate)
    g_previousTerminate();
  std::abort();
}

// The port the last Listening line in `text` names, if any.
bool listeningPort(const std::string &text, uint16_t &port)
{
  const auto at = text.rfind(LISTENING);
  if (at == std::string::npos)
    return false;
  const auto digits = at + std::strlen(LISTENING);
  auto end = digits;
  while (
      end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
    ++end;
  return protocol::parsePort(text.substr(digits, end - digits), port);
}

std::string readFile(const std::filesystem::path &path, std::uintmax_t from)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  file.seekg(std::streamoff(from));
  return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string exitText(int status)
{
  if (WIFEXITED(status))
    return "exit status " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status))
    return "signal " + std::to_string(WTERMSIG(status));
  return "status " + std::to_string(status);
}

} // namespace

ServerProcess::ServerProcess(std::vector<std::string> command,
    std::filesystem::path dataRoot,
    std::filesystem::path logPath)
    : m_command(std::move(command)),
      m_dataRoot(std::move(dataRoot)),
      m_logPath(std::move(logPath))
{}

ServerProcess::~ServerProcess()
{
  stop();
}

bool ServerProcess::start(std::string *error)
{
  if (running()) {
    if (error)
      *error = "the server is already running";
    return false;
  }
  if (m_command.empty()) {
    if (error)
      *error = "no server command";
    return false;
  }

  // The log is appended so a restart's post-mortem shows both lifetimes;
  // what belongs to this start is everything past its current end.
  {
    std::ofstream touch(m_logPath, std::ios::app);
    if (!touch) {
      if (error)
        *error = "cannot open " + m_logPath.string() + " for writing";
      return false;
    }
  }
  m_logStart = std::filesystem::file_size(m_logPath);
  m_listening = false;

  auto argv = m_command;
  argv.push_back("--port");
  argv.push_back(std::to_string(m_port));
  argv.push_back("--data-root");
  argv.push_back(m_dataRoot.string());
  std::vector<char *> argvPointers;
  for (auto &arg : argv)
    argvPointers.push_back(arg.data());
  argvPointers.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions,
      STDOUT_FILENO,
      m_logPath.c_str(),
      O_WRONLY | O_CREAT | O_APPEND,
      0644);
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  pid_t pid = 0;
  const int rc = posix_spawnp(
      &pid, argv[0].c_str(), &actions, nullptr, argvPointers.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    if (error)
      *error = "cannot start " + argv[0] + ": " + std::strerror(rc);
    return false;
  }
  m_pid = pid;
  g_spawnedServerPid = pid;
  return true;
}

ServerProcess::Start ServerProcess::check(std::string *error)
{
  if (m_listening)
    return Start::Listening;
  if (m_pid == 0) {
    if (error)
      *error = "the server is not running";
    return Start::Failed;
  }
  // The log is read before the process is checked: what the server wrote
  // just before exiting counts.
  const auto text = logSinceStart();
  const bool listening = listeningPort(text, m_port);
  // A server that has exited is not Listening, whatever it printed first.
  if (reap(false)) {
    if (text.find(NO_DEVICE) != std::string::npos)
      return Start::NoDevice;
    if (error) {
      *error = std::string("the server exited ")
          + (listening ? "after" : "before") + " listening ("
          + exitText(m_exitStatus) + ")";
    }
    return Start::Failed;
  }
  if (!listening)
    return Start::Starting;
  m_listening = true;
  return Start::Listening;
}

ServerProcess::Start ServerProcess::awaitListening(
    std::chrono::milliseconds deadline, std::string *error)
{
  const auto until = std::chrono::steady_clock::now() + deadline;
  for (;;) {
    const auto start = check(error);
    if (start != Start::Starting)
      return start;
    if (std::chrono::steady_clock::now() >= until) {
      if (error) {
        *error = "the server did not reach Listening within "
            + std::to_string(deadline.count()) + " ms";
      }
      return Start::Failed;
    }
    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

bool ServerProcess::running()
{
  return m_pid != 0 && !reap(false);
}

void ServerProcess::kill()
{
  if (m_pid == 0)
    return;
  ::kill(m_pid, SIGKILL);
  reap(true);
}

void ServerProcess::stop()
{
  if (m_pid == 0)
    return;
  ::kill(m_pid, SIGTERM);
  const auto until = std::chrono::steady_clock::now() + STOP_GRACE;
  while (!reap(false)) {
    if (std::chrono::steady_clock::now() >= until) {
      ::kill(m_pid, SIGKILL);
      reap(true);
      break;
    }
    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

uint16_t ServerProcess::port() const
{
  return m_port;
}

const std::filesystem::path &ServerProcess::dataRoot() const
{
  return m_dataRoot;
}

const std::filesystem::path &ServerProcess::logPath() const
{
  return m_logPath;
}

std::string ServerProcess::log() const
{
  return readFile(m_logPath, 0);
}

std::string ServerProcess::logSinceStart() const
{
  return readFile(m_logPath, m_logStart);
}

bool ServerProcess::reap(bool block)
{
  if (m_pid == 0)
    return true;
  int status = 0;
  const pid_t reaped = ::waitpid(m_pid, &status, block ? 0 : WNOHANG);
  if (reaped == 0)
    return false;
  // reaped < 0: no such child any more (ECHILD); it is gone either way.
  m_exitStatus = reaped > 0 ? status : 0;
  if (g_spawnedServerPid == m_pid)
    g_spawnedServerPid = 0;
  m_pid = 0;
  m_listening = false;
  return true;
}

void stopSpawnedServerOnDeath()
{
  // Idempotent: a second std::set_terminate() would chain onTerminate to
  // itself.
  static bool installed = false;
  if (installed)
    return;
  installed = true;
  ::signal(SIGTERM, onDeathSignal);
  ::signal(SIGINT, onDeathSignal);
  g_previousTerminate = std::set_terminate(onTerminate);
}

} // namespace vsr::scivis_studio::test_client
