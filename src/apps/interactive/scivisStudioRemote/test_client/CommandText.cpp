// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandText.h"
#include "AnyText.h"
#include "Script.h"
// std
#include <cctype>

namespace vsr::scivis_studio::test_client {

std::string lower(std::string text)
{
  for (auto &c : text)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::string upper(std::string text)
{
  for (auto &c : text)
    c = char(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

std::string quoted(const std::string &text)
{
  return "\"" + text + "\"";
}

std::string join(const std::vector<std::string> &items, const char *sep)
{
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i)
      out += sep;
    out += items[i];
  }
  return out;
}

const char *boolText(bool value)
{
  return value ? "true" : "false";
}

bool parseBool(const std::string &text, bool &out)
{
  const auto word = lower(text);
  if (word == "true" || word == "on" || word == "1")
    out = true;
  else if (word == "false" || word == "off" || word == "0")
    out = false;
  else
    return false;
  return true;
}

std::string nodeText(const SceneNodeRef &node)
{
  return node.layerName + ":" + std::to_string(node.nodeIndex);
}

std::string objectRefText(const SceneObjectRef &ref)
{
  return shortTypeName(ref.type) + ":" + std::to_string(ref.objectIndex);
}

bool parseObjectRef(const std::string &typeText,
    const std::string &indexText,
    SceneObjectRef &ref,
    std::string &error)
{
  const auto type = parseAnariType(typeText);
  if (!type || !anari::isObject(*type)) {
    error = "not a scene object type: " + typeText;
    return false;
  }
  unsigned long long index = 0;
  if (!parseNonNegative(indexText, index)) {
    error = "not an object index: " + indexText;
    return false;
  }
  ref.type = *type;
  ref.objectIndex = size_t(index);
  return true;
}

bool parseObjectRefArgs(const std::vector<std::string> &args,
    size_t first,
    SceneObjectRef &ref,
    size_t &consumed,
    std::string &error)
{
  if (first >= args.size()) {
    error = "missing object reference (<type> <index> or <type:index>)";
    return false;
  }
  const auto colon = args[first].find(':');
  if (colon != std::string::npos) {
    consumed = 1;
    return parseObjectRef(args[first].substr(0, colon),
        args[first].substr(colon + 1),
        ref,
        error);
  }
  if (first + 1 >= args.size()) {
    error = "missing object index after " + args[first];
    return false;
  }
  consumed = 2;
  return parseObjectRef(args[first], args[first + 1], ref, error);
}

} // namespace vsr::scivis_studio::test_client
