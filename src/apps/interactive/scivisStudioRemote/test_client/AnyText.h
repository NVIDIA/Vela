// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/Any.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <charconv>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::test_client {

/*
 * ANARI values as script text: the type names a script writes (`camera`,
 * `float32_vec3`, case-insensitive, the SDK's `ANARI_` spelling accepted
 * too), one token per component on the way in (`set-param`), and the one
 * string form of a mirror value on the way out (`assert`, `dump-ui-state`).
 * Numbers print as the shortest text that reads back as the same number, so a
 * float32 set from "0.9" prints "0.9". Nothing here knows about the runner.
 *
 * Example:
 *   vsr::core::Any value;
 *   std::string error;
 *   if (anyFromTokens(*parseAnariType("float32_vec3"), {"1", "2", "3"},
 *           value, error))
 *     assert(anyText(value) == "1 2 3");
 */

// Script spelling ("camera", "float32_vec3") or the SDK's ("ANARI_CAMERA");
// case-insensitive. Empty when no ANARI type has that name.
std::optional<anari::DataType> parseAnariType(const std::string &text);

// "ANARI_FLOAT32_VEC3" -> "float32_vec3"
std::string shortTypeName(anari::DataType type);

// Builds the Any a `set-param` names: bool, string, or any scalar, vector,
// matrix or box whose components are an integer or float type, with one
// token per component. Integer components must fit their type (uint8 300 is
// a typo, not 44). False with the reason on anything else.
bool anyFromTokens(anari::DataType type,
    const std::vector<std::string> &tokens,
    vsr::core::Any &out,
    std::string &error);

// The string form of a mirror value: strings verbatim, bools as true/false,
// object references as type:index, numbers space-separated per component.
std::string anyText(const vsr::core::Any &value);

// The whole text is one number strtod accepts. False otherwise.
bool parseDouble(const std::string &text, double &out);

// The shortest text that reads back as the same number, so a float32 set from
// "0.9" prints "0.9" and one set from "0.123456789" prints "0.12345679", which
// `assert ... == 0.123456789` still equates at float32 precision.
template <typename T>
std::string numberText(T value);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline std::string numberText(T value)
{
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

} // namespace vsr::scivis_studio::test_client
