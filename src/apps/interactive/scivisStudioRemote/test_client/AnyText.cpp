// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AnyText.h"
#include "CommandText.h"
#include "Script.h"
// std
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace vsr::scivis_studio::test_client {

using vsr::core::Any;

namespace {

// The scalar an ANARI value type is made of, read off its name.
enum class Component
{
  None,
  Float32,
  Float64,
  Int8,
  Int16,
  Int32,
  Int64,
  UInt8,
  UInt16,
  UInt32,
  UInt64
};

struct ComponentInfo
{
  const char *prefix;
  Component component;
  size_t size;
};

// clang-format off
constexpr ComponentInfo COMPONENTS[] = {
    {"ANARI_FLOAT32", Component::Float32, 4},
    {"ANARI_FLOAT64", Component::Float64, 8},
    {"ANARI_INT8",    Component::Int8,    1},
    {"ANARI_INT16",   Component::Int16,   2},
    {"ANARI_INT32",   Component::Int32,   4},
    {"ANARI_INT64",   Component::Int64,   8},
    {"ANARI_UINT8",   Component::UInt8,   1},
    {"ANARI_UINT16",  Component::UInt16,  2},
    {"ANARI_UINT32",  Component::UInt32,  4},
    {"ANARI_UINT64",  Component::UInt64,  8},
};
// clang-format on

const ComponentInfo *componentOf(anari::DataType type)
{
  const std::string name = anari::toString(type);
  for (const auto &info : COMPONENTS) {
    const size_t n = std::strlen(info.prefix);
    if (name.compare(0, n, info.prefix) == 0
        && (name.size() == n || name[n] == '_'))
      return &info;
  }
  return nullptr;
}

// Rejects what T cannot hold rather than wrapping it: uint8 300 is a typo,
// not 44.
template <typename T>
bool storeInteger(const std::string &text, std::byte *dst)
{
  T typed{};
  if constexpr (std::is_signed_v<T>) {
    long long value = 0;
    if (!parseInteger(text, value) || value < std::numeric_limits<T>::min()
        || value > std::numeric_limits<T>::max())
      return false;
    typed = static_cast<T>(value);
  } else {
    unsigned long long value = 0;
    if (!parseNonNegative(text, value) || value > std::numeric_limits<T>::max())
      return false;
    typed = static_cast<T>(value);
  }
  std::memcpy(dst, &typed, sizeof(typed));
  return true;
}

template <typename T>
bool storeFloat(const std::string &text, std::byte *dst)
{
  double value = 0;
  if (!parseDouble(text, value))
    return false;
  const T typed = static_cast<T>(value);
  std::memcpy(dst, &typed, sizeof(typed));
  return true;
}

bool storeComponent(
    Component component, const std::string &text, std::byte *dst)
{
  switch (component) {
  case Component::Float32:
    return storeFloat<float>(text, dst);
  case Component::Float64:
    return storeFloat<double>(text, dst);
  case Component::Int8:
    return storeInteger<int8_t>(text, dst);
  case Component::Int16:
    return storeInteger<int16_t>(text, dst);
  case Component::Int32:
    return storeInteger<int32_t>(text, dst);
  case Component::Int64:
    return storeInteger<int64_t>(text, dst);
  case Component::UInt8:
    return storeInteger<uint8_t>(text, dst);
  case Component::UInt16:
    return storeInteger<uint16_t>(text, dst);
  case Component::UInt32:
    return storeInteger<uint32_t>(text, dst);
  case Component::UInt64:
    return storeInteger<uint64_t>(text, dst);
  case Component::None:
    break;
  }
  return false;
}

template <typename T>
std::string componentText(const std::byte *src)
{
  T value;
  std::memcpy(&value, src, sizeof(value));
  return numberText(value);
}

std::string componentText(Component component, const std::byte *src)
{
  switch (component) {
  case Component::Float32:
    return componentText<float>(src);
  case Component::Float64:
    return componentText<double>(src);
  case Component::Int8:
    return componentText<int8_t>(src);
  case Component::Int16:
    return componentText<int16_t>(src);
  case Component::Int32:
    return componentText<int32_t>(src);
  case Component::Int64:
    return componentText<int64_t>(src);
  case Component::UInt8:
    return componentText<uint8_t>(src);
  case Component::UInt16:
    return componentText<uint16_t>(src);
  case Component::UInt32:
    return componentText<uint32_t>(src);
  case Component::UInt64:
    return componentText<uint64_t>(src);
  case Component::None:
    break;
  }
  return "?";
}

} // namespace

// anari_cpp only offers type -> name, so this scans the value range the SDK's
// enums occupy (object types 5xx, scalars 1xxx, matrices 2xxx); a script names
// a type a handful of times, so no table.
std::optional<anari::DataType> parseAnariType(const std::string &text)
{
  std::string name = upper(text);
  if (name.rfind("ANARI_", 0) != 0)
    name = "ANARI_" + name;
  constexpr int MAX_ANARI_TYPE_VALUE = 4096;
  for (int v = 0; v < MAX_ANARI_TYPE_VALUE; ++v) {
    if (name == anari::toString(anari::DataType(v)))
      return anari::DataType(v);
  }
  return {};
}

std::string shortTypeName(anari::DataType type)
{
  std::string name = anari::toString(type);
  if (name.rfind("ANARI_", 0) == 0)
    name.erase(0, 6);
  return lower(name);
}

bool anyFromTokens(anari::DataType type,
    const std::vector<std::string> &tokens,
    Any &out,
    std::string &error)
{
  if (type == ANARI_STRING) {
    if (tokens.size() != 1) {
      error = "string takes exactly one value (quote it to include spaces)";
      return false;
    }
    out = Any(tokens.front());
    return true;
  }
  if (type == ANARI_BOOL) {
    if (tokens.size() != 1) {
      error = "bool takes exactly one value";
      return false;
    }
    const auto text = lower(tokens.front());
    if (text == "true" || text == "1")
      out = Any(true);
    else if (text == "false" || text == "0")
      out = Any(false);
    else {
      error = "bool value must be true, false, 1 or 0, got: " + tokens.front();
      return false;
    }
    return true;
  }

  const auto *component = componentOf(type);
  if (!component || anari::isObject(type) || anari::isArray(type)) {
    error = std::string("unsupported value type ") + anari::toString(type);
    return false;
  }
  const size_t count = anari::sizeOf(type) / component->size;
  if (tokens.size() != count) {
    error = shortTypeName(type) + " takes " + std::to_string(count)
        + " value(s), got " + std::to_string(tokens.size());
    return false;
  }
  std::vector<std::byte> bytes(anari::sizeOf(type));
  for (size_t i = 0; i < count; ++i) {
    if (!storeComponent(component->component,
            tokens[i],
            bytes.data() + i * component->size)) {
      error = "not a " + shortTypeName(type) + " component: " + tokens[i];
      return false;
    }
  }
  out = Any(type, bytes.data());
  return true;
}

std::string anyText(const Any &value)
{
  if (!value.valid())
    return "<none>";
  const auto type = value.type();
  if (type == ANARI_STRING)
    return value.getString();
  if (type == ANARI_BOOL)
    return value.get<bool>() ? "true" : "false";
  if (anari::isObject(type)) {
    return shortTypeName(type) + ":" + std::to_string(value.getAsObjectIndex());
  }
  const auto *component = componentOf(type);
  if (!component)
    return std::string("<") + shortTypeName(type) + ">";
  const auto *bytes = static_cast<const std::byte *>(value.data());
  const size_t count = anari::sizeOf(type) / component->size;
  std::string out;
  for (size_t i = 0; i < count; ++i) {
    if (i)
      out += ' ';
    out += componentText(component->component, bytes + i * component->size);
  }
  return out;
}

bool parseDouble(const std::string &text, double &out)
{
  if (text.empty())
    return false;
  errno = 0;
  char *end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0')
    return false;
  out = value;
  return true;
}

} // namespace vsr::scivis_studio::test_client
