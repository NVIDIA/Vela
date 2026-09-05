// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerProcess.h"
// std
#include <cerrno>
#include <cstring>
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

// The number after the last Listening line in `text`, if any.
bool listeningPort(const std::string &text, uint16_t &port)
{
  const auto at = text.rfind(LISTENING);
  if (at == std::string::npos)
    return false;
  const auto digits = at + std::strlen(LISTENING);
  unsigned long value = 0;
  size_t consumed = 0;
  try {
    value = std::stoul(text.substr(digits), &consumed);
  } catch (const std::exception &) {
    return false;
  }
  if (consumed == 0 || value > 65535)
    return false;
  port = uint16_t(value);
  return true;
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
  if (listeningPort(text, m_port)) {
    m_listening = true;
    return Start::Listening;
  }
  if (!reap(false))
    return Start::Starting;
  if (text.find(NO_DEVICE) != std::string::npos)
    return Start::NoDevice;
  if (error) {
    *error =
        "the server exited before listening (" + exitText(m_exitStatus) + ")";
  }
  return Start::Failed;
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
  m_pid = 0;
  m_listening = false;
  return true;
}

} // namespace vsr::scivis_studio::test_client
