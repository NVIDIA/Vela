// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::scivis_studio::test_client {

/*
 * The test client's script language: one command per line, `#` starts a
 * comment, blank lines are ignored, and `;` separates several commands on one
 * line (how `-e "connect; ping"` arrives). Tokens split on whitespace; double
 * quotes group a token that contains spaces and drop the quotes.
 *
 * Example:
 *   std::vector<Command> commands;
 *   std::string error;
 *   if (!parseScript("connect; set-param camera 0 fovy float32 0.9",
 *           commands, &error))
 *     std::cerr << error;
 *   // commands[1] == {"set-param", {"camera","0","fovy","float32","0.9"}, 1}
 */

struct Command
{
  std::string name;
  std::vector<std::string> args;
  // 1-based line of the script the command came from; several commands on
  // one line share it.
  int lineNumber{0};
  // The command as written (quotes intact, comment stripped), for the
  // `OK <line>` / `FAIL <line>` records.
  std::string text;
};

// Appends every command of `script` to `out`, numbering lines from
// `firstLine`. False with the reason on an unterminated quote; `out` then
// holds the commands parsed before the offending line.
bool parseScript(std::string_view script,
    std::vector<Command> &out,
    std::string *error = nullptr,
    int firstLine = 1);

// Splits `text` into tokens with the quoting rules above. False on an
// unterminated quote.
bool tokenize(std::string_view text,
    std::vector<std::string> &out,
    std::string *error = nullptr);

// Removes a trailing `timeout=<ms>` argument, if any, and returns the span it
// named. Empty (and the args untouched) when the last argument is not a
// timeout; a timeout with a malformed value is an error.
std::optional<std::chrono::milliseconds> takeTimeoutSuffix(
    Command &command, std::string *error = nullptr);

// Integer arguments, on the command line and in scripts: the whole text is
// one decimal number that fits, no sign for parseNonNegative. False otherwise.
bool parseInteger(std::string_view text, long long &out);
bool parseNonNegative(std::string_view text, unsigned long long &out);
// A non-negative millisecond count that still adds to a steady_clock time
// point without overflowing; timeouts and sleeps go through here.
bool parseMilliseconds(std::string_view text, std::chrono::milliseconds &out);

} // namespace vsr::scivis_studio::test_client
