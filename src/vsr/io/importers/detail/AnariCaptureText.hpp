// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/*
 * The text of an ANARI Capture, read into blocks but not yet interpreted.
 * This is the grammar of format versions 1 and 2 in the ANARI-SDK `capture`
 * device's README
 * (src/devices/capture/README.md), which is the contract: Vela reads it
 * itself rather than linking the SDK's diff library.
 */
namespace vsr::io::detail::anari_capture {

// Version 2 added the `calls` block; version 1 is otherwise the same.
constexpr int OLDEST_SUPPORTED_VERSION = 1;
constexpr int NEWEST_SUPPORTED_VERSION = 2;

// A parameter of an object block, or a field of an array block (`type` empty).
// A value that is a list (`[` ... `]`) is held in `items`, one per line.
struct Entry
{
  std::string name;
  std::string type;
  std::string value;
  bool isList{false};
  std::vector<std::string> items;
};

struct Block
{
  bool isArray() const;
  const Entry *find(std::string_view name) const;

  std::string id; // "camera@0"
  std::string label; // "camera"
  std::optional<std::string> subtype; // unquoted
  std::vector<Entry> entries; // in file order
};

struct CaptureText
{
  int version{0};
  std::vector<std::string> extensions;
  std::vector<std::string> callCounts; // "total <n>", then "<function> <n>"
  std::vector<std::string> warnings;
  std::string frameId;
  std::map<std::string, Block> blocks; // every object and array, by ID
};

// Reads a capture's text. Returns an empty optional, and says why in `error`,
// for text that is not a capture, has a version this reader does not know, has
// no frame, or has a line the grammar does not allow.
std::optional<CaptureText> parseCaptureText(
    std::string_view text, std::string &error);

// The ANARI type a name like "ANARI_FLOAT32_VEC3" spells, or ANARI_UNKNOWN.
anari::DataType dataTypeFromName(std::string_view name);

// Decodes one value or array element of `type` written as the capture writes
// it (a bare value or a tuple) into `out`, which receives anari::sizeOf(type)
// bytes. Handles numeric, fixed-point, boolean and ANARI_DATA_TYPE values;
// returns false for anything else or for text that does not fit the type.
bool decodeValue(anari::DataType type, std::string_view text, std::byte *out);

// The string a quoted capture string spells, escapes undone.
std::optional<std::string> unquote(std::string_view quoted);

} // namespace vsr::io::detail::anari_capture
