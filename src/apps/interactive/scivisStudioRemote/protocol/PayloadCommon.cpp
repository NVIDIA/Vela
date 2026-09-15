// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadCommon.h"

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

void writePathList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<std::filesystem::path> &paths)
{
  std::vector<std::string> items;
  items.reserve(paths.size());
  for (const auto &p : paths)
    items.push_back(p.generic_string());
  writeStringList(parent, name, items);
}

bool readPathList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<std::filesystem::path> &out)
{
  std::vector<std::string> items;
  if (!readStringList(parent, name, items))
    return false;
  out.assign(items.begin(), items.end());
  return true;
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

} // namespace vsr::scivis_studio::protocol
