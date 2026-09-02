// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerOptions.h"
// std
#include <sstream>

namespace vsr::scivis_studio::server {

namespace {

void setError(std::string *error, const std::string &text)
{
  if (error)
    *error = text;
}

} // namespace

bool parseServerOptions(const std::vector<std::string> &args,
    ServerOptions &options,
    std::string *error)
{
  options = {};

  for (size_t i = 1; i < args.size(); ++i) {
    const auto &arg = args[i];

    if (arg == "-h" || arg == "--help") {
      options.showHelp = true;
      return true;
    }

    const bool takesValue = arg == "--port" || arg == "--library"
        || arg == "--data-root" || arg == "--project";
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

    if (arg == "--port") {
      if (!protocol::parsePort(value, options.port)) {
        setError(
            error, "--port requires an integer in 1..65535, got: " + value);
        return false;
      }
    } else if (arg == "--library") {
      options.library = value;
    } else if (arg == "--data-root") {
      options.dataRoots.emplace_back(value);
    } else if (arg == "--project") {
      if (!options.projectDirectory.empty()) {
        setError(error, "multiple --project directories were specified");
        return false;
      }
      options.projectDirectory = value;
    }
  }

  if (options.dataRoots.empty()) {
    if (options.projectDirectory.empty()) {
      setError(error, "at least one --data-root is required");
      return false;
    }
    // The parent, not the project directory itself: a project's datasets
    // normally sit beside it, and pointing at the project dir would make
    // every sibling dataset unreachable.
    auto project =
        std::filesystem::absolute(options.projectDirectory).lexically_normal();
    if (project.filename().empty()) // "dir/" normalizes with an empty leaf
      project = project.parent_path();
    options.dataRoots.push_back(project.parent_path());
  }

  return true;
}

std::string serverUsage(const std::string &programName)
{
  std::ostringstream out;
  out << "usage: " << programName
      << " [--port N] [--library NAME] --data-root DIR [--data-root DIR ...]"
         " [--project DIR]\n"
         "\n"
         "  --port N          TCP port to listen on (default "
      << protocol::DEFAULT_PORT
      << ")\n"
         "  --library NAME    ANARI library to render with (default: first"
         " entry of the\n"
         "                    ANARI library list; VSR_ANARI_LIBRARIES, else"
         " helide)\n"
         "  --data-root DIR   directory clients may browse and operate under;"
         " repeatable.\n"
         "                    Required unless --project is given, in which"
         " case the\n"
         "                    project directory's parent is the default root\n"
         "  --project DIR     project to open at startup (default: a fresh"
         " unsaved project)\n"
         "  -h, --help        show this help\n";
  return out.str();
}

} // namespace vsr::scivis_studio::server
