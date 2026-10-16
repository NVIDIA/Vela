// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RenderDeviceConfig.h"
// vsr_app
#include "vsr/app/ANARIDeviceManager.h"
// std
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace vsr::render_cli {
namespace {

void require(bool valid, const std::string &message)
{
  if (!valid)
    throw std::runtime_error(message);
}

std::string normalize(const std::string &device)
{
  const auto id = vsr::app::DeviceIdentifier::parse(device);
  require(bool(id), "Invalid Device Identifier: " + device);
  return id->str();
}

bool contains(const char *const *values, const std::string &value)
{
  for (; values && *values; ++values)
    if (value == *values)
      return true;
  return false;
}

void status(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[ANARI selection] %s\n", message);
}

struct MetadataDevice
{
  MetadataDevice() = default;
  ~MetadataDevice();
  VSR_NOT_COPYABLE(MetadataDevice)
  VSR_NOT_MOVEABLE(MetadataDevice)

  void load(const std::string &identifier);

  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

void MetadataDevice::load(const std::string &identifier)
{
  const auto id = vsr::app::DeviceIdentifier::parse(identifier);
  require(bool(id), "Invalid Device Identifier: " + identifier);
  library = anari::loadLibrary(id->library.c_str(), status);
  require(library != nullptr,
      "Cannot load selected ANARI library: " + id->library + " (device "
          + identifier + "); check --device and library search paths");
  const auto **subtypes = anariGetDeviceSubtypes(library);
  require(!subtypes || !*subtypes || contains(subtypes, id->subtype),
      "Unsupported device subtype: " + id->subtype
          + " in selected ANARI library " + id->library);
  device = anari::newDevice(library, id->subtype.c_str());
  require(
      device != nullptr, "Cannot create selected ANARI device: " + identifier);
  anari::commitParameters(device, device);
}

MetadataDevice::~MetadataDevice()
{
  if (device)
    anari::release(device, device);
  if (library)
    anari::unloadLibrary(library);
}

struct NumericType
{
  anari::DataType type;
  anari::DataType scalar;
  size_t components;
};

// Only fixed-size arithmetic values have a safe CLI representation. Matrices,
// packed colors, pointers and object handles deliberately do not.
#define NUMERIC_TYPE_ENTRIES(BASE)                                             \
  {ANARI_##BASE, ANARI_##BASE, 1}, {ANARI_##BASE##_VEC2, ANARI_##BASE, 2},     \
      {ANARI_##BASE##_VEC3, ANARI_##BASE, 3},                                  \
      {ANARI_##BASE##_VEC4, ANARI_##BASE, 4}
constexpr NumericType NUMERIC_TYPES[] = {NUMERIC_TYPE_ENTRIES(INT8),
    NUMERIC_TYPE_ENTRIES(UINT8),
    NUMERIC_TYPE_ENTRIES(INT16),
    NUMERIC_TYPE_ENTRIES(UINT16),
    NUMERIC_TYPE_ENTRIES(INT32),
    NUMERIC_TYPE_ENTRIES(UINT32),
    NUMERIC_TYPE_ENTRIES(INT64),
    NUMERIC_TYPE_ENTRIES(UINT64),
    NUMERIC_TYPE_ENTRIES(FLOAT32),
    NUMERIC_TYPE_ENTRIES(FLOAT64)};
#undef NUMERIC_TYPE_ENTRIES

const NumericType *numericType(anari::DataType type)
{
  for (const auto &numeric : NUMERIC_TYPES)
    if (numeric.type == type)
      return &numeric;
  return nullptr;
}

std::vector<std::string> components(const std::string &text)
{
  std::vector<std::string> result;
  size_t begin = 0;
  do {
    const auto end = text.find(',', begin);
    result.push_back(text.substr(begin, end - begin));
    if (end == text.npos)
      break;
    begin = end + 1;
  } while (true);
  return result;
}

template <typename T>
std::optional<vsr::core::Any> decodeNumeric(
    const NumericType &type, const std::vector<std::string> &parts)
{
  if (parts.size() != type.components)
    return {};
  std::array<T, 4> values{};
  for (size_t i = 0; i < parts.size(); ++i) {
    const auto &part = parts[i];
    const auto parsed =
        std::from_chars(part.data(), part.data() + part.size(), values[i]);
    if (part.empty() || parsed.ec != std::errc()
        || parsed.ptr != part.data() + part.size())
      return {};
    if constexpr (std::is_floating_point_v<T>)
      if (!std::isfinite(values[i]))
        return {};
  }
  return vsr::core::Any(type.type, values.data());
}

std::optional<vsr::core::Any> decode(
    anari::DataType type, const std::string &text)
{
  if (type == ANARI_STRING)
    return vsr::core::Any(text);
  if (type == ANARI_BOOL) {
    if (text == "true" || text == "1")
      return vsr::core::Any(true);
    if (text == "false" || text == "0")
      return vsr::core::Any(false);
    return {};
  }
  const auto *numeric = numericType(type);
  if (!numeric)
    return {};
  const auto parts = components(text);
  switch (numeric->scalar) {
#define DECODE(BASE, TYPE)                                                     \
  case ANARI_##BASE:                                                           \
    return decodeNumeric<TYPE>(*numeric, parts)
    DECODE(INT8, int8_t);
    DECODE(UINT8, uint8_t);
    DECODE(INT16, int16_t);
    DECODE(UINT16, uint16_t);
    DECODE(INT32, int32_t);
    DECODE(UINT32, uint32_t);
    DECODE(INT64, int64_t);
    DECODE(UINT64, uint64_t);
    DECODE(FLOAT32, float);
    DECODE(FLOAT64, double);
#undef DECODE
  default:
    return {};
  }
}

template <typename T>
bool inRange(const vsr::core::Any &value,
    size_t count,
    const void *minimum,
    const void *maximum)
{
  std::array<T, 4> values{}, lower{}, upper{};
  const auto size = count * sizeof(T);
  std::memcpy(values.data(), value.data(), size);
  if (minimum)
    std::memcpy(lower.data(), minimum, size);
  if (maximum)
    std::memcpy(upper.data(), maximum, size);
  for (size_t i = 0; i < count; ++i) {
    if constexpr (std::is_floating_point_v<T>)
      if (!std::isfinite(values[i]))
        return false;
    if ((minimum && values[i] < lower[i]) || (maximum && values[i] > upper[i]))
      return false;
  }
  return true;
}

void validateValue(anari::Device device,
    const std::string &subtype,
    const std::string &name,
    const vsr::core::Any &value)
{
  const auto info = [&](const char *property, anari::DataType type) {
    return anariGetParameterInfo(device,
        ANARI_RENDERER,
        subtype.c_str(),
        name.c_str(),
        value.type(),
        property,
        type);
  };
  if (value.type() == ANARI_STRING) {
    const auto *values =
        static_cast<const char *const *>(info("value", ANARI_STRING_LIST));
    require(!values || !*values || contains(values, value.getString()),
        "Invalid renderer string value for '" + name
            + "': " + value.getString());
  } else if (const auto *numeric = numericType(value.type())) {
    const auto *minimum = info("minimum", value.type());
    const auto *maximum = info("maximum", value.type());
    bool valid = false;
    switch (numeric->scalar) {
#define CHECK(BASE, TYPE)                                                      \
  case ANARI_##BASE:                                                           \
    valid = inRange<TYPE>(value, numeric->components, minimum, maximum);       \
    break
      CHECK(INT8, int8_t);
      CHECK(UINT8, uint8_t);
      CHECK(INT16, int16_t);
      CHECK(UINT16, uint16_t);
      CHECK(INT32, int32_t);
      CHECK(UINT32, uint32_t);
      CHECK(INT64, int64_t);
      CHECK(UINT64, uint64_t);
      CHECK(FLOAT32, float);
      CHECK(FLOAT64, double);
#undef CHECK
    default:
      break;
    }
    require(valid,
        "Invalid renderer parameter '" + name
            + "': outside metadata bounds or nonfinite");
  }
}

vsr::core::Any convert(anari::Device device,
    const std::string &subtype,
    const std::string &name,
    const std::string &text)
{
  const auto *parameters =
      static_cast<const ANARIParameter *>(anariGetObjectInfo(device,
          ANARI_RENDERER,
          subtype.c_str(),
          "parameter",
          ANARI_PARAMETER_LIST));
  bool known = false;
  std::optional<vsr::core::Any> value;
  std::string types;
  for (const auto *p = parameters; p && p->name; ++p) {
    if (name != p->name)
      continue;
    known = true;
    types += std::string(" ") + anari::toString(p->type);
    if (auto candidate = decode(p->type, text)) {
      require(!value || value->type() == candidate->type(),
          "Ambiguous renderer conversion for '" + name
              + "'; metadata allows multiple types");
      value = std::move(candidate);
    }
  }
  require(known, "Unknown renderer parameter: " + name);
  require(bool(value),
      "Invalid or unsupported renderer value for '" + name
          + "'; advertised types:" + types
          + ". Object/array construction is not supported; vectors use commas.");
  validateValue(device, subtype, name, *value);
  return std::move(*value);
}

} // namespace

std::string resolveDevice(const std::optional<std::string> &explicitDevice,
    const std::string &savedDevice)
{
  if (explicitDevice)
    return normalize(*explicitDevice);
  if (!savedDevice.empty())
    return normalize(savedDevice);
  if (const auto *configured = std::getenv("VSR_ANARI_LIBRARIES")) {
    const std::string list(configured);
    const auto begin = list.find_first_not_of(',');
    if (begin != list.npos)
      return normalize(list.substr(begin, list.find(',', begin) - begin));
  }
  return std::getenv("ANARI_LIBRARY") ? "environment" : "helide";
}

vsr::scene::Object resolveRenderer(const std::string &device,
    const std::string &savedDevice,
    const vsr::scene::Object *savedRenderer,
    const std::optional<std::string> &explicitSubtype,
    const RendererAssignments &assignments)
{
  MetadataDevice metadata;
  metadata.load(device);
  const bool sameDevice =
      !savedDevice.empty() && normalize(savedDevice) == device;
  const std::string subtype = explicitSubtype.value_or(
      sameDevice && savedRenderer ? savedRenderer->subtype().str() : "default");
  require(contains(
              anariGetObjectSubtypes(metadata.device, ANARI_RENDERER), subtype),
      "Unsupported renderer subtype: " + subtype + " on device " + device);
  const bool preserve =
      sameDevice && savedRenderer && savedRenderer->subtype().str() == subtype;
  vsr::scene::Object renderer(ANARI_RENDERER, subtype.c_str());
  if (preserve) {
    renderer.setName(savedRenderer->name());
    for (size_t i = 0; i < savedRenderer->numParameters(); ++i) {
      const auto name = savedRenderer->parameterNameAt(i);
      const auto &saved = savedRenderer->parameterAt(i);
      renderer.addParameter(name)
          .setDescription(saved.description().c_str())
          .setStringValues(saved.stringValues())
          .setValue(saved.value())
          .setMin(saved.min())
          .setMax(saved.max())
          .setStringSelection(saved.stringSelection())
          .setUsage(saved.usage())
          .setEnabled(saved.isEnabled());
    }
  } else {
    const auto *parameters =
        static_cast<const ANARIParameter *>(anariGetObjectInfo(metadata.device,
            ANARI_RENDERER,
            subtype.c_str(),
            "parameter",
            ANARI_PARAMETER_LIST));
    for (const auto *p = parameters; p && p->name; ++p) {
      if (!numericType(p->type) && p->type != ANARI_BOOL
          && p->type != ANARI_STRING)
        continue;
      const auto *value = anariGetParameterInfo(metadata.device,
          ANARI_RENDERER,
          subtype.c_str(),
          p->name,
          p->type,
          "default",
          p->type);
      // Missing metadata defaults stay unset, letting the device initialize
      // them. Do not synthesize zero/empty values for unauthored parameters.
      if (!value)
        continue;
      const auto *existing = renderer.parameter(p->name);
      require(!existing || existing->value().type() == p->type,
          std::string("Ambiguous renderer metadata defaults for '") + p->name
              + "'");
      renderer.setParameter(p->name, vsr::core::Any(p->type, value));
    }
    if (savedRenderer || (!savedDevice.empty() && !sameDevice))
      std::fprintf(stderr,
          "Renderer reset: %s; discard saved parameters and initialize %s/%s defaults\n",
          sameDevice ? "renderer subtype changed" : "device changed",
          device.c_str(),
          subtype.c_str());
  }
  for (const auto &[name, text] : assignments) {
    const auto value = convert(metadata.device, subtype, name, text);
    renderer.setParameter(name.c_str(), value)->setEnabled(true);
  }
  return renderer;
}

} // namespace vsr::render_cli
