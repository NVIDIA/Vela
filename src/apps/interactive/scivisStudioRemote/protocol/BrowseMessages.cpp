// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "BrowseMessages.h"
#include "PayloadCommon.h"

namespace vsr::scivis_studio::protocol {

namespace {

// Ordered lists travel as children "0", "1", ... of parent[name]; an empty
// list writes nothing (leaf-only serialization would drop the node anyway).
template <typename T>
void writeNodeList(
    vsr::core::DataNode &parent, const char *name, const std::vector<T> &items)
{
  if (items.empty())
    return;
  auto &list = parent[name];
  for (size_t i = 0; i < items.size(); ++i)
    toNode(items[i], list[std::to_string(i)]);
}

template <typename T>
bool readNodeList(
    const vsr::core::DataNode &parent, const char *name, std::vector<T> &out)
{
  out.clear();
  const auto *list = parent.child(name);
  if (!list)
    return true;
  bool ok = true;
  list->foreach_child_const([&](const vsr::core::DataNode &item) {
    T value;
    if (!fromNode(item, value)) {
      ok = false;
      return;
    }
    out.push_back(std::move(value));
  });
  return ok;
}

} // namespace

const char *toString(EntryKind kind)
{
  switch (kind) {
  case EntryKind::File:
    return "File";
  case EntryKind::Directory:
    return "Directory";
  case EntryKind::ProjectDirectory:
    return "ProjectDirectory";
  }
  return "File";
}

std::optional<EntryKind> entryKindFromString(std::string_view name)
{
  if (name == "File")
    return EntryKind::File;
  if (name == "Directory")
    return EntryKind::Directory;
  if (name == "ProjectDirectory")
    return EntryKind::ProjectDirectory;
  return {};
}

// Requests ///////////////////////////////////////////////////////////////////

void toNode(const ListRoots &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
}

bool fromNode(const vsr::core::DataNode &n, ListRoots &r)
{
  return readChild(n, "requestId", r.requestId);
}

void toNode(const ListDirectory &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writePath(n, "directory", r.directory);
}

bool fromNode(const vsr::core::DataNode &n, ListDirectory &r)
{
  return readChild(n, "requestId", r.requestId)
      && readPath(n, "directory", r.directory);
}

// Results ////////////////////////////////////////////////////////////////////

void toNode(const DirectoryEntry &e, vsr::core::DataNode &n)
{
  writeChild(n, "name", e.name);
  writeChild(n, "kind", std::string(toString(e.kind)));
  writeChild(n, "size", e.size);
  writeChild(n, "mtimeSeconds", e.mtimeSeconds);
}

bool fromNode(const vsr::core::DataNode &n, DirectoryEntry &e)
{
  if (!readChild(n, "name", e.name)
      || !readEnumChild(n, "kind", e.kind, entryKindFromString))
    return false;
  e.size = readChildOr(n, "size", uint64_t(0));
  e.mtimeSeconds = readChildOr(n, "mtimeSeconds", int64_t(0));
  return true;
}

void toNode(const ListRootsResult &r, vsr::core::DataNode &n)
{
  std::vector<std::string> roots;
  roots.reserve(r.roots.size());
  for (const auto &p : r.roots)
    roots.push_back(p.generic_string());
  writeStringList(n, "roots", roots);
}

bool fromNode(const vsr::core::DataNode &n, ListRootsResult &r)
{
  std::vector<std::string> roots;
  if (!readStringList(n, "roots", roots))
    return false;
  r.roots.assign(roots.begin(), roots.end());
  return true;
}

void toNode(const ListDirectoryResult &r, vsr::core::DataNode &n)
{
  writeNodeList(n, "entries", r.entries);
}

bool fromNode(const vsr::core::DataNode &n, ListDirectoryResult &r)
{
  return readNodeList(n, "entries", r.entries);
}

} // namespace vsr::scivis_studio::protocol
