// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CommandRunner.h"
// std
#include <string>
#include <vector>

namespace vsr::scivis_studio::test_client {

/*
 * Command line of scivisStudioTestClient. The script comes from --script,
 * from one or more -e "cmd; cmd" (in order), or from stdin when neither is
 * given; everything else configures the CommandRunner. --help and --markdown
 * print the command vocabulary from CommandRunner::commands(), the first as
 * the usage text, the second as the README's command table.
 *
 * Example:
 *   TestClientOptions options;
 *   std::string error;
 *   if (!parseTestClientOptions(args, options, &error)) {
 *     std::cerr << error << '\n' << testClientUsage(args[0]);
 *     return 2;
 *   }
 */
struct TestClientOptions
{
  RunnerOptions runner;
  // Empty means no --script was given.
  std::string scriptPath;
  // Every -e argument, in command-line order.
  std::vector<std::string> inlineScripts;
  bool showHelp{false};
  bool showMarkdown{false};
};

// args[0] is the program name. False with the reason on an unknown flag or a
// missing or malformed value. --help and --markdown short-circuit: the flag is
// set and the rest is not validated.
bool parseTestClientOptions(const std::vector<std::string> &args,
    TestClientOptions &options,
    std::string *error = nullptr);

// Flags, the command vocabulary (from CommandRunner::commands(), grouped by
// kind) and the assert value names.
std::string testClientUsage(const std::string &programName);

// The command vocabulary as one Markdown table, sorted by name: the table
// test_client/README.md carries, so a test can check the two agree.
std::string testClientCommandTable();

} // namespace vsr::scivis_studio::test_client
