// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::server {

/*
 * The server's Data Roots: the directories every path-taking operation
 * (project open/save, imports, archives, Remote Browse) must stay inside. A
 * guardrail against confused clients, not a security boundary. Roots and
 * requests are both canonicalized (symlinks in existing prefixes resolved,
 * a non-existent tail kept so save targets can be checked) and compared by
 * path component, so "/data2" is not inside "/data". The --project
 * directory given at launch is a root too when no other root contains it.
 *
 * Example:
 *   DataRoots roots(options.dataRoots, options.projectDirectory);
 *   std::string error;
 *   if (auto path = roots.resolve(request.sourcePath, &error))
 *     import(*path);
 *   else
 *     reply(makeErrorReply(request.requestId, error));
 */
struct DataRoots
{
  DataRoots() = default;
  DataRoots(const std::vector<std::filesystem::path> &roots,
      const std::filesystem::path &projectDirectory = {});

  // Canonical roots, in the order given (the implicit project root last).
  const std::vector<std::filesystem::path> &roots() const;

  // The canonical form of `path` when it is absolute and lies inside a root;
  // empty otherwise, with `error` naming the offending path.
  std::optional<std::filesystem::path> resolve(
      const std::filesystem::path &path, std::string *error = nullptr) const;
  bool isInside(const std::filesystem::path &path) const;

 private:
  std::vector<std::filesystem::path> m_roots;
};

// weakly_canonical() with the fallbacks the roots need: an unresolvable path
// is made absolute and normalized, and a trailing separator is dropped.
std::filesystem::path canonicalizePath(const std::filesystem::path &path);

} // namespace vsr::scivis_studio::server
