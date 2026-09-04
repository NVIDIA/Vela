// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ViewportMessages.h"
#include "PayloadCommon.h"

namespace vsr::scivis_studio::protocol {

using vsr::rendering::AOVType;

const char *toString(AOVType type)
{
  switch (type) {
  case AOVType::NONE:
    return "NONE";
  case AOVType::DEPTH:
    return "DEPTH";
  case AOVType::ALBEDO:
    return "ALBEDO";
  case AOVType::NORMAL:
    return "NORMAL";
  case AOVType::EDGES:
    return "EDGES";
  case AOVType::OBJECT_ID:
    return "OBJECT_ID";
  case AOVType::PRIMITIVE_ID:
    return "PRIMITIVE_ID";
  case AOVType::INSTANCE_ID:
    return "INSTANCE_ID";
  }
  return "Unknown";
}

std::optional<AOVType> aovTypeFromString(std::string_view name)
{
  return enumFromName(name, AOVType::NONE, AOVType::INSTANCE_ID, toString);
}

// Histogram //////////////////////////////////////////////////////////////////

void toNode(const ArrayHistogramResult &r, vsr::core::DataNode &n)
{
  if (!r.bins.empty())
    n["bins"].setValueAsArray(r.bins);
  writeChild(n, "minValue", r.minValue);
  writeChild(n, "maxValue", r.maxValue);
  writeChild(n, "nonFinite", r.nonFinite);
}

bool fromNode(const vsr::core::DataNode &n, ArrayHistogramResult &r)
{
  r.nonFinite = 0;
  if (!readChild(n, "minValue", r.minValue)
      || !readChild(n, "maxValue", r.maxValue)
      || !readOptionalChild(n, "nonFinite", r.nonFinite))
    return false;
  r.bins.clear();
  const auto *bins = n.child("bins");
  if (!bins)
    return true;
  const uint64_t *data = nullptr;
  size_t count = 0;
  bins->getValueAsArray(&data, &count);
  if (!data)
    return false;
  r.bins.assign(data, data + count);
  return true;
}

} // namespace vsr::scivis_studio::protocol
