// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadCommon.h"
// vsr_core
#include "vsr/core/FlatMap.hpp"
// std
#include <cstring>

namespace vsr::scivis_studio::protocol {

// Strings, lists, paths //////////////////////////////////////////////////////

void writeStringList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<std::string> &items)
{
  if (items.empty())
    return;
  auto &list = parent[name];
  for (size_t i = 0; i < items.size(); ++i)
    list[std::to_string(i)] = items[i];
}

bool readStringList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<std::string> &out)
{
  out.clear();
  const auto *list = parent.child(name);
  if (!list)
    return true;
  bool ok = true;
  list->foreach_child_const([&](const vsr::core::DataNode &item) {
    if (!item.getValue().is<std::string>()) {
      ok = false;
      return;
    }
    out.push_back(item.getValueAs<std::string>());
  });
  return ok;
}

void writePath(vsr::core::DataNode &parent,
    const char *name,
    const std::filesystem::path &path)
{
  parent[name] = path.generic_string();
}

bool readPath(const vsr::core::DataNode &parent,
    const char *name,
    std::filesystem::path &out)
{
  std::string text;
  if (!readChild(parent, name, text))
    return false;
  out = std::filesystem::path(text);
  return true;
}

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

// Opaque subtrees ////////////////////////////////////////////////////////////

SubtreePtr makeSubtree()
{
  return std::make_shared<vsr::core::DataTree>();
}

void writeSubtree(
    vsr::core::DataNode &parent, const char *name, const SubtreePtr &subtree)
{
  if (!subtree)
    return;
  parent[name] = subtree->root();
}

SubtreePtr readSubtree(const vsr::core::DataNode &parent, const char *name)
{
  const auto *c = parent.child(name);
  if (!c)
    return {};
  auto subtree = makeSubtree();
  subtree->root() = *c;
  return subtree;
}

// Scene identity /////////////////////////////////////////////////////////////

void toNode(
    const vsr::scivis_studio::SceneObjectRef &ref, vsr::core::DataNode &node)
{
  node["type"] = std::string(toString(ref.type));
  writeChild(node, "objectIndex", ref.objectIndex);
}

bool fromNode(
    const vsr::core::DataNode &node, vsr::scivis_studio::SceneObjectRef &ref)
{
  std::string typeName;
  if (!readChild(node, "type", typeName))
    return false;
  const auto type = anariTypeFromString(typeName);
  if (!type)
    return false;
  size_t index = VSR_INVALID_INDEX;
  if (!readChild(node, "objectIndex", index))
    return false;
  ref.type = *type;
  ref.objectIndex = index;
  return true;
}

void toNode(
    const vsr::scivis_studio::SceneNodeRef &ref, vsr::core::DataNode &node)
{
  node["layerName"] = ref.layerName;
  writeChild(node, "nodeIndex", ref.nodeIndex);
}

bool fromNode(
    const vsr::core::DataNode &node, vsr::scivis_studio::SceneNodeRef &ref)
{
  std::string layerName;
  size_t index = VSR_INVALID_INDEX;
  if (!readChild(node, "layerName", layerName)
      || !readChild(node, "nodeIndex", index))
    return false;
  ref.layerName = std::move(layerName);
  ref.nodeIndex = index;
  return true;
}

} // namespace vsr::scivis_studio::protocol
