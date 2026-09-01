// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <filesystem>
#include <string>
#include <vector>

namespace vsr::scivis_studio::server {

constexpr int DEFAULT_PORT = 12345;

/*
 * Launch configuration of scivisStudioServer. The server is config-free by
 * design: argv is the only source of settings, so a job script or ssh session
 * can reproduce a launch exactly.
 *
 * Data Roots guard every path-taking operation. At least one is required;
 * when only --project is given, the project directory's parent becomes the
 * sole root, since a project's datasets normally live beside it and the user
 * has already named that location.
 *
 * Example:
 *   ServerOptions options;
 *   std::string error;
 *   if (!parseServerOptions(args, options, &error)) {
 *     std::cerr << error << '\n' << serverUsage(args[0]);
 *     return 1;
 *   }
 */
struct ServerOptions
{
  // 0 asks the OS for a free port (tests); the parser only accepts 1..65535.
  int port{DEFAULT_PORT};
  // ANARI library to render with; empty means the first entry of the
  // ANARIDeviceManager library list, exactly what the monolith's viewport
  // starts with.
  std::string library;
  std::vector<std::filesystem::path> dataRoots;
  // Empty means the server starts on a fresh unsaved project.
  std::filesystem::path projectDirectory;
  bool showHelp{false};
};

// args[0] is the program name. False with the reason on an unknown flag, a
// missing or malformed value, or when no Data Root can be determined.
// --help short-circuits: showHelp is set and the rest is not validated.
bool parseServerOptions(const std::vector<std::string> &args,
    ServerOptions &options,
    std::string *error = nullptr);

std::string serverUsage(const std::string &programName);

} // namespace vsr::scivis_studio::server
