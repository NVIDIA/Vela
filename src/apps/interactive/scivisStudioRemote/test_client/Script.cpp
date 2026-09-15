// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Script.h"
// std
#include <cctype>
#include <charconv>
#include <system_error>

namespace vsr::scivis_studio::test_client {

namespace {

constexpr std::string_view TIMEOUT_PREFIX = "timeout=";

bool isSpace(char c)
{
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string_view trim(std::string_view text)
{
  while (!text.empty() && isSpace(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back()))
    text.remove_suffix(1);
  return text;
}

// Cuts a line into its `;`-separated commands, honouring quotes and dropping
// a `#` comment. False on an unterminated quote.
bool splitLine(std::string_view line,
    std::vector<std::string_view> &out,
    std::string *error)
{
  size_t start = 0;
  bool quoted = false;
  size_t i = 0;
  for (; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      quoted = !quoted;
    } else if (!quoted && c == '#') {
      break;
    } else if (!quoted && c == ';') {
      out.push_back(line.substr(start, i - start));
      start = i + 1;
    }
  }
  if (quoted) {
    if (error)
      *error = "unterminated quote";
    return false;
  }
  out.push_back(line.substr(start, i - start));
  return true;
}

// Splits `text` into tokens with the quoting rules of the language. False on
// an unterminated quote.
bool tokenize(
    std::string_view text, std::vector<std::string> &out, std::string *error)
{
  out.clear();
  std::string current;
  bool inToken = false;
  bool quoted = false;
  for (const char c : text) {
    if (c == '"') {
      quoted = !quoted;
      inToken = true; // "" is an empty token, not nothing
    } else if (!quoted && isSpace(c)) {
      if (inToken) {
        out.push_back(current);
        current.clear();
        inToken = false;
      }
    } else {
      current.push_back(c);
      inToken = true;
    }
  }
  if (quoted) {
    if (error)
      *error = "unterminated quote";
    return false;
  }
  if (inToken)
    out.push_back(current);
  return true;
}

} // namespace

bool parseScript(
    std::string_view script, std::vector<Command> &out, std::string *error)
{
  int lineNumber = 1;
  while (!script.empty()) {
    const auto newline = script.find('\n');
    std::string_view line = script.substr(0, newline);
    script = newline == std::string_view::npos ? std::string_view{}
                                               : script.substr(newline + 1);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);

    std::vector<std::string_view> pieces;
    std::string pieceError;
    if (!splitLine(line, pieces, &pieceError)) {
      if (error)
        *error = "line " + std::to_string(lineNumber) + ": " + pieceError;
      return false;
    }
    for (const auto piece : pieces) {
      const auto text = trim(piece);
      if (text.empty())
        continue;
      Command command;
      if (!tokenize(text, command.args, &pieceError)) {
        if (error)
          *error = "line " + std::to_string(lineNumber) + ": " + pieceError;
        return false;
      }
      if (command.args.empty())
        continue;
      command.name = command.args.front();
      command.args.erase(command.args.begin());
      command.lineNumber = lineNumber;
      command.text = std::string(text);
      out.push_back(std::move(command));
    }
    ++lineNumber;
  }
  return true;
}

std::optional<std::chrono::milliseconds> takeTimeoutSuffix(
    Command &command, std::string *error)
{
  if (command.args.empty())
    return {};
  const std::string &last = command.args.back();
  if (last.compare(0, TIMEOUT_PREFIX.size(), TIMEOUT_PREFIX) != 0)
    return {};
  std::chrono::milliseconds ms;
  if (!parseMilliseconds(
          std::string_view(last).substr(TIMEOUT_PREFIX.size()), ms)) {
    if (error)
      *error = "malformed timeout: " + last;
    return {};
  }
  command.args.pop_back();
  return ms;
}

bool expandVariables(
    Command &command, const VariableLookup &lookup, std::string *error)
{
  const auto isNameChar = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
  };
  std::vector<std::string> expanded;
  expanded.reserve(command.args.size());
  for (const auto &arg : command.args) {
    std::string out;
    for (size_t i = 0; i < arg.size();) {
      if (arg[i] != '$') {
        out.push_back(arg[i++]);
        continue;
      }
      size_t end = i + 1;
      while (end < arg.size() && isNameChar(arg[end]))
        ++end;
      if (end == i + 1) {
        out.push_back(arg[i++]); // a bare `$` is text
        continue;
      }
      const auto name = arg.substr(i + 1, end - i - 1);
      const auto value = lookup(name);
      if (!value) {
        if (error)
          *error = "unknown variable $" + name;
        return false;
      }
      out += *value;
      i = end;
    }
    expanded.push_back(std::move(out));
  }
  command.args = std::move(expanded);
  return true;
}

bool parseInteger(std::string_view text, long long &out)
{
  long long value = 0;
  const auto result = std::from_chars(text.data(), text.end(), value);
  if (result.ec != std::errc{} || result.ptr != text.end())
    return false;
  out = value;
  return true;
}

bool parseNonNegative(std::string_view text, unsigned long long &out)
{
  unsigned long long value = 0;
  const auto result = std::from_chars(text.data(), text.end(), value);
  if (result.ec != std::errc{} || result.ptr != text.end())
    return false;
  out = value;
  return true;
}

bool parseMilliseconds(std::string_view text, std::chrono::milliseconds &out)
{
  // Half of what a steady_clock duration holds, so `now() + out` cannot wrap.
  constexpr auto MAX_MS = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::duration::max())
                              .count()
      / 2;
  unsigned long long value = 0;
  if (!parseNonNegative(text, value)
      || value > static_cast<unsigned long long>(MAX_MS))
    return false;
  out = std::chrono::milliseconds(value);
  return true;
}

} // namespace vsr::scivis_studio::test_client
