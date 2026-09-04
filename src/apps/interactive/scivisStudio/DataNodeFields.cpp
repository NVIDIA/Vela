// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataNodeFields.h"

#include "vsr/core/FlatMap.hpp"

#include <cstring>

namespace vsr::scivis_studio {

// ANARI type names ///////////////////////////////////////////////////////////

const char *toString(anari::DataType type)
{
  return anari::toString(type);
}

std::optional<anari::DataType> anariTypeFromString(std::string_view name)
{
  // anari_cpp only offers type -> name; invert it once over the value range
  // the SDK's enums occupy (object types 5xx, scalars 1xxx, matrices 2xxx).
  static const auto table = [] {
    vsr::core::FlatMap<std::string, anari::DataType> m;
    constexpr int MAX_ANARI_TYPE_VALUE = 4096;
    for (int v = 0; v < MAX_ANARI_TYPE_VALUE; ++v) {
      const auto t = anari::DataType(v);
      const char *n = anari::toString(t);
      if (t == ANARI_UNKNOWN || std::strcmp(n, "ANARI_UNKNOWN") != 0)
        m.set(n, t);
    }
    return m;
  }();
  const auto *found = table.at(std::string(name));
  if (!found)
    return {};
  return *found;
}

// Scene identity /////////////////////////////////////////////////////////////

void toNode(const SceneObjectRef &ref, vsr::core::DataNode &node)
{
  writeChild(node, "type", std::string(toString(ref.type)));
  writeChild(node, "objectIndex", ref.objectIndex);
}

bool fromNode(const vsr::core::DataNode &node, SceneObjectRef &ref)
{
  SceneObjectRef out;
  if (!readEnumChild(node, "type", out.type, anariTypeFromString)
      || !readChild(node, "objectIndex", out.objectIndex))
    return false;
  ref = out;
  return true;
}

void toNode(const SceneNodeRef &ref, vsr::core::DataNode &node)
{
  writeChild(node, "layerName", ref.layerName);
  writeChild(node, "nodeIndex", ref.nodeIndex);
}

bool fromNode(const vsr::core::DataNode &node, SceneNodeRef &ref)
{
  SceneNodeRef out;
  if (!readChild(node, "layerName", out.layerName)
      || !readChild(node, "nodeIndex", out.nodeIndex))
    return false;
  ref = std::move(out);
  return true;
}

} // namespace vsr::scivis_studio
