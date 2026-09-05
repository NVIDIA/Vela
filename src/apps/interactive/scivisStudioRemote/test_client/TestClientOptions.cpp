// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TestClientOptions.h"
#include "Script.h"
// vsr_scivis_studio_protocol
#include "StudioProtocol.h"
// std
#include <ostream>
#include <sstream>

namespace vsr::scivis_studio::test_client {

namespace {

void setError(std::string *error, const std::string &text)
{
  if (error)
    *error = text;
}

// `name usage`, the command as a script writes it.
std::string commandLine(const CommandRunner::CommandSpec &spec)
{
  std::string line = spec.name;
  if (*spec.usage)
    line += std::string(" ") + spec.usage;
  return line;
}

// `text` wrapped at `width` columns on spaces, every line after the first
// indented by `indent`.
void appendWrapped(
    std::ostream &out, const std::string &text, size_t indent, size_t width)
{
  size_t column = indent;
  bool first = true;
  std::istringstream words(text);
  std::string word;
  while (words >> word) {
    if (!first && column + 1 + word.size() > width) {
      out << '\n' << std::string(indent, ' ');
      column = indent;
    } else if (!first) {
      out << ' ';
      ++column;
    }
    out << word;
    column += word.size();
    first = false;
  }
  out << '\n';
}

// The commands of one kind, `name usage` in the first column and the summary
// wrapped in the second (on its own lines when the first overflows).
void appendCommandHelp(std::ostream &out, CommandRunner::Kind kind)
{
  constexpr size_t SUMMARY_COLUMN = 32;
  constexpr size_t WIDTH = 80;
  for (const auto &spec : CommandRunner::commands()) {
    if (spec.kind != kind)
      continue;
    const auto line = "  " + commandLine(spec);
    out << line;
    if (line.size() + 1 < SUMMARY_COLUMN)
      out << std::string(SUMMARY_COLUMN - line.size(), ' ');
    else
      out << '\n' << std::string(SUMMARY_COLUMN, ' ');
    appendWrapped(out, spec.summary, SUMMARY_COLUMN, WIDTH);
  }
}

const char *kindText(CommandRunner::Kind kind)
{
  switch (kind) {
  case CommandRunner::Kind::Session:
    return "session";
  case CommandRunner::Kind::Request:
    return "request";
  case CommandRunner::Kind::Wait:
    return "wait";
  }
  return "?";
}

// A Markdown table cell: `|` would end it.
std::string escapedCell(const std::string &text)
{
  std::string out;
  for (const char c : text) {
    if (c == '|')
      out += '\\';
    out += c;
  }
  return out;
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
    if (arg == "--markdown") {
      options.showMarkdown = true;
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
         "  --markdown        print the command table as Markdown (the"
         " README's) and exit\n"
         "\n"
         "Waiting commands take a trailing timeout=MS and FAIL as soon as"
         " the\n"
         "connection is Lost. An Error the server sends during any command"
         " but\n"
         "expect-error FAILs that command. `$name` expands to a variable in"
         " any\n"
         "argument; replies fill $lastShotId $lastLightRigId $lastCameraRigId\n"
         "$lastColorMapId $lastObjectRef $lastObjectType $lastObjectIndex\n"
         "$lastLightLayer $lastLightNode $lastDatasetId $lastTaskId"
         " $lastTaskMessage\n"
         "$lastRequestId $dataRoot.\n"
         "\n"
         "Session commands (no prefix applies):\n";
  appendCommandHelp(out, CommandRunner::Kind::Session);
  out << "\n"
         "Request commands (each sends one request, awaits its ProjectOpReply"
         " and\n"
         "prints it; `expect-fail <request>` makes ok=false the OK outcome,\n"
         "`no-wait <request>` sends without awaiting the reply):\n";
  appendCommandHelp(out, CommandRunner::Kind::Request);
  out << "\n"
         "Waits (`expect-fail` applies):\n";
  appendCommandHelp(out, CommandRunner::Kind::Wait);
  out << "\n"
         "assert values:\n ";
  for (const auto &name : CommandRunner::assertNames())
    out << ' ' << name;
  out << '\n';
  return out.str();
}

std::string testClientCommandTable()
{
  std::ostringstream out;
  out << "| command | kind | does |\n"
         "|---------|------|------|\n";
  for (const auto &spec : CommandRunner::commands()) {
    out << "| `" << escapedCell(commandLine(spec)) << "` | "
        << kindText(spec.kind) << " | " << escapedCell(spec.summary) << " |\n";
  }
  return out.str();
}

} // namespace vsr::scivis_studio::test_client
