// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TestClientOptions.h"
#include "Script.h"
// vsr_scivis_studio_protocol
#include "StudioEndpoint.h"
// std
#include <sstream>

namespace vsr::scivis_studio::test_client {

namespace {

void setError(std::string *error, const std::string &text)
{
  if (error)
    *error = text;
}

} // namespace

bool parseTestClientOptions(const std::vector<std::string> &args,
    TestClientOptions &options,
    std::string *error)
{
  options = {};

  for (size_t i = 1; i < args.size(); ++i) {
    const auto &arg = args[i];

    if (arg == "-h" || arg == "--help") {
      options.showHelp = true;
      return true;
    }
    if (arg == "--keep-going") {
      options.runner.keepGoing = true;
      continue;
    }
    if (arg == "--quiet-events") {
      options.runner.quietEvents = true;
      continue;
    }

    const bool takesValue = arg == "--host" || arg == "--port"
        || arg == "--script" || arg == "-e" || arg == "--timeout";
    if (!takesValue) {
      setError(error, "unknown option: " + arg);
      return false;
    }
    if (i + 1 >= args.size()) {
      setError(error, arg + " requires a value");
      return false;
    }
    const auto &value = args[++i];
    if (value.empty()) {
      setError(error, arg + " requires a non-empty value");
      return false;
    }

    if (arg == "--host") {
      options.runner.host = value;
    } else if (arg == "--port") {
      if (!protocol::parsePort(value, options.runner.port)) {
        setError(
            error, "--port requires an integer in 1..65535, got: " + value);
        return false;
      }
    } else if (arg == "--script") {
      if (!options.scriptPath.empty()) {
        setError(error, "multiple --script files were specified");
        return false;
      }
      options.scriptPath = value;
    } else if (arg == "-e") {
      options.inlineScripts.push_back(value);
    } else if (arg == "--timeout") {
      if (!parseMilliseconds(value, options.runner.timeout)) {
        setError(error,
            "--timeout requires a non-negative count of milliseconds that fits"
            " a deadline, got: "
                + value);
        return false;
      }
    }
  }

  if (!options.scriptPath.empty() && !options.inlineScripts.empty()) {
    setError(error, "--script and -e cannot be combined");
    return false;
  }
  return true;
}

std::string testClientUsage(const std::string &programName)
{
  std::ostringstream out;
  out << "usage: " << programName
      << " [--host H] [--port N] [--timeout MS] [--keep-going]"
         " [--quiet-events]\n"
         "       [--script FILE | -e \"cmd; cmd\" ...]\n"
         "\n"
         "Runs a script of commands against a scivisStudioServer and prints"
         " one record\n"
         "per line: OK <command>, FAIL <command>: <reason>, or EVT <Name>"
         " key=value ...\n"
         "for every server message. Exit status 0 iff no command failed."
         " Log lines go\n"
         "to stderr. Without --script or -e the script is read from stdin.\n"
         "\n"
         "  --host H          server host `connect` uses by default"
         " (127.0.0.1)\n"
         "  --port N          server port `connect` uses by default ("
      << protocol::DEFAULT_PORT
      << ")\n"
         "  --script FILE     read commands from FILE, one per line; # starts"
         " a comment\n"
         "  -e \"cmd; cmd\"     run these commands (repeatable, in order)\n"
         "  --timeout MS      deadline of every waiting command (5000)\n"
         "  --keep-going      continue after a FAIL; the exit status is still"
         " non-zero\n"
         "  --quiet-events    print no EVT lines except from the dump-*"
         " commands\n"
         "  -h, --help        show this help\n"
         "\n"
         "Commands:\n";
  for (const auto &line : CommandRunner::commandHelp())
    out << "  " << line << '\n';
  out << " ";
  for (const auto &name : CommandRunner::assertNames())
    out << ' ' << name;
  out << '\n';
  return out.str();
}

} // namespace vsr::scivis_studio::test_client
