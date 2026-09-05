// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

/*
 * A scratch directory of this process' own under the system temp directory,
 * removed when the fixture goes away. Tests that write files (meshes, saved
 * projects, server logs) hold one so two test binaries running at once, or
 * the same one twice, never collide on a name and never leave debris.
 *
 * Example:
 *   ScopedFixtureDirectory scratch("vsr_studio_e2e_");
 *   writeTriangleObj(scratch.path / "triangle.obj");
 */
struct ScopedFixtureDirectory
{
  explicit ScopedFixtureDirectory(const std::string &prefix);
  ~ScopedFixtureDirectory();

  ScopedFixtureDirectory(const ScopedFixtureDirectory &) = delete;
  ScopedFixtureDirectory &operator=(const ScopedFixtureDirectory &) = delete;

  std::filesystem::path path;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline ScopedFixtureDirectory::ScopedFixtureDirectory(const std::string &prefix)
{
  // create_directory reports whether it was this process that made the
  // directory, so retrying on a taken name is what makes the choice safe
  // rather than merely unlikely.
  std::random_device entropy;
  const auto root = std::filesystem::temp_directory_path();
  do {
    path = root / (prefix + std::to_string(entropy()));
  } while (!std::filesystem::create_directory(path));
}

inline ScopedFixtureDirectory::~ScopedFixtureDirectory()
{
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}
