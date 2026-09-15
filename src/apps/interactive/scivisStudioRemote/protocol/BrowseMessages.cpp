// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "BrowseMessages.h"

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
  return "Unknown";
}

std::optional<EntryKind> entryKindFromString(std::string_view name)
{
  return enumFromName(
      name, EntryKind::File, EntryKind::ProjectDirectory, toString);
}

} // namespace vsr::scivis_studio::protocol
