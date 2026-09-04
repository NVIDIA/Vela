// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Remote Browse: the two stateless requests that replace the native file
 * dialogs. The server only lists what lives under its Data Roots; the client
 * filters by extension and decides what to show. Project directories are
 * marked, never hidden. Paths on the wire are absolute server paths.
 *
 * Example:
 *   ListDirectory req;
 *   req.requestId = nextId();
 *   req.directory = "/data/runs";
 *   channel.send(encode(req));
 */

struct ListRoots
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ListRoots;
  uint64_t requestId{0};
};

struct ListDirectory
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ListDirectory;
  uint64_t requestId{0};
  std::filesystem::path directory;
};

enum class EntryKind
{
  File,
  Directory,
  ProjectDirectory
};

struct DirectoryEntry
{
  std::string name;
  EntryKind kind{EntryKind::File};
  uint64_t size{0};
  int64_t mtimeSeconds{0};
};

// Results carried in a ProjectOpReply's `results` subtree.

struct ListRootsResult
{
  std::vector<std::filesystem::path> roots;
};

struct ListDirectoryResult
{
  std::vector<DirectoryEntry> entries;
};

// Enumerator names ("File", "Directory", "ProjectDirectory"), "Unknown"
// otherwise.
const char *toString(EntryKind kind);
std::optional<EntryKind> entryKindFromString(std::string_view name);

// Every payload is a fields() description (PayloadCommon.h). requestId,
// directory, name and kind are required; size and mtimeSeconds default to 0;
// lists keep their order and an absent list reads as empty.

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, ListRoots &r)
{
  v.required("requestId", r.requestId);
}

template <typename V>
void fields(V &v, ListDirectory &r)
{
  v.required("requestId", r.requestId);
  v.required("directory", r.directory);
}

template <typename V>
void fields(V &v, DirectoryEntry &e)
{
  v.required("name", e.name);
  v.requiredEnum("kind", e.kind, toString, entryKindFromString);
  v.optional("size", e.size);
  v.optional("mtimeSeconds", e.mtimeSeconds);
}

template <typename V>
void fields(V &v, ListRootsResult &r)
{
  v.list("roots", r.roots);
}

template <typename V>
void fields(V &v, ListDirectoryResult &r)
{
  v.list("entries", r.entries);
}

} // namespace vsr::scivis_studio::protocol
