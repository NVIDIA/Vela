// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RemoteBrowse.h"
// vsr_scivis_studio_model
#include "ProjectSerialization.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <cctype>
#include <chrono>

namespace vsr::scivis_studio::server {

using namespace protocol;

namespace {

// file_clock has no portable epoch in C++17; anchor it to system_clock now.
int64_t toUnixSeconds(std::filesystem::file_time_type time)
{
  using namespace std::chrono;
  const auto sinceNow = time - std::filesystem::file_time_type::clock::now();
  const auto sys =
      system_clock::now() + duration_cast<system_clock::duration>(sinceNow);
  return duration_cast<seconds>(sys.time_since_epoch()).count();
}

bool lessCaseInsensitive(const std::string &a, const std::string &b)
{
  return std::lexicographical_compare(
      a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x))
            < std::tolower(static_cast<unsigned char>(y));
      });
}

// The cheap marker OpenProject validates for real: a manifest file (current
// or legacy spelling) directly inside the directory.
bool holdsProjectManifest(const std::filesystem::path &directory)
{
  std::error_code ec;
  return std::filesystem::exists(
      resolveProjectFileForRead(directory / PROJECT_MANIFEST_FILENAME), ec);
}

} // namespace

ListRootsResult listRoots(const DataRoots &roots)
{
  ListRootsResult result;
  result.roots = roots.roots();
  return result;
}

bool listDirectory(const DataRoots &roots,
    const std::filesystem::path &directory,
    ListDirectoryResult &out,
    std::string *error)
{
  const auto real = roots.resolve(directory, error);
  if (!real)
    return false;

  std::error_code ec;
  if (!std::filesystem::is_directory(*real, ec)) {
    if (error)
      *error = "not a directory: '" + directory.generic_string() + "'";
    return false;
  }

  std::filesystem::directory_iterator it(*real, ec), end;
  if (ec) {
    if (error) {
      *error =
          "cannot list '" + directory.generic_string() + "': " + ec.message();
    }
    return false;
  }

  out.entries.clear();
  for (; it != end; it.increment(ec)) {
    if (ec)
      break;
    const auto &entry = *it;
    std::error_code entryError;
    const auto status = entry.status(entryError);
    if (entryError) {
      vsr::core::logWarning("[StudioServer] skipping '%s' in listing: %s",
          entry.path().string().c_str(),
          entryError.message().c_str());
      continue;
    }

    DirectoryEntry item;
    item.name = entry.path().filename().string();
    if (std::filesystem::is_directory(status)) {
      item.kind = holdsProjectManifest(entry.path())
          ? EntryKind::ProjectDirectory
          : EntryKind::Directory;
    } else if (std::filesystem::is_regular_file(status)) {
      item.kind = EntryKind::File;
      item.size = entry.file_size(entryError);
      if (entryError)
        item.size = 0;
    } else {
      continue; // sockets, fifos, devices: nothing Studio can open
    }
    const auto mtime = entry.last_write_time(entryError);
    item.mtimeSeconds = entryError ? 0 : toUnixSeconds(mtime);
    out.entries.push_back(std::move(item));
  }
  if (ec) {
    vsr::core::logWarning("[StudioServer] listing '%s' stopped early: %s",
        directory.string().c_str(),
        ec.message().c_str());
  }

  std::sort(out.entries.begin(),
      out.entries.end(),
      [](const DirectoryEntry &a, const DirectoryEntry &b) {
        const bool aDir = a.kind != EntryKind::File;
        const bool bDir = b.kind != EntryKind::File;
        if (aDir != bDir)
          return aDir;
        return lessCaseInsensitive(a.name, b.name);
      });
  return true;
}

} // namespace vsr::scivis_studio::server
