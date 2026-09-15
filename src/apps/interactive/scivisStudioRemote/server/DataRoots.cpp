// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataRoots.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>

namespace vsr::scivis_studio::server {

namespace {

// True when every component of `prefix` opens `path`.
bool hasPathPrefix(
    const std::filesystem::path &path, const std::filesystem::path &prefix)
{
  const auto mismatch =
      std::mismatch(prefix.begin(), prefix.end(), path.begin(), path.end());
  return mismatch.first == prefix.end();
}

} // namespace

std::filesystem::path canonicalizePath(const std::filesystem::path &path)
{
  std::error_code ec;
  auto real = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    ec.clear();
    real = std::filesystem::absolute(path, ec).lexically_normal();
  }
  if (real.filename().empty() && real.has_parent_path()) // "dir/" -> "dir"
    real = real.parent_path();
  return real;
}

DataRoots::DataRoots(const std::vector<std::filesystem::path> &roots,
    const std::filesystem::path &projectDirectory)
{
  for (const auto &root : roots) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
      vsr::core::logWarning(
          "[StudioServer] Data Root '%s' is not an existing directory",
          root.string().c_str());
    }
    m_roots.push_back(canonicalizePath(root));
  }
  if (!projectDirectory.empty() && !isInside(projectDirectory))
    m_roots.push_back(canonicalizePath(projectDirectory));
}

const std::vector<std::filesystem::path> &DataRoots::roots() const
{
  return m_roots;
}

std::optional<std::filesystem::path> DataRoots::resolve(
    const std::filesystem::path &path, std::string *error) const
{
  if (path.empty() || !path.is_absolute()) {
    if (error)
      *error = "path must be absolute: '" + path.generic_string() + "'";
    return {};
  }
  auto real = canonicalizePath(path);
  for (const auto &root : m_roots) {
    if (hasPathPrefix(real, root))
      return real;
  }
  if (error) {
    *error = "path is outside the server's Data Roots: '"
        + path.generic_string() + "'";
  }
  return {};
}

bool DataRoots::isInside(const std::filesystem::path &path) const
{
  return resolve(path).has_value();
}

} // namespace vsr::scivis_studio::server
