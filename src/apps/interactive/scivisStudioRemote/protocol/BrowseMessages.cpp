// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "BrowseMessages.h"
#include "PayloadMacros.h"

namespace vsr::scivis_studio::protocol {

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

VSR_STUDIO_BARE_REQUEST(ListRoots)

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
  writePathList(n, "roots", r.roots);
}

bool fromNode(const vsr::core::DataNode &n, ListRootsResult &r)
{
  return readPathList(n, "roots", r.roots);
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
