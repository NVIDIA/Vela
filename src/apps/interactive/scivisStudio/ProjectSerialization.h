// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Project.h"

#include "vsr/core/DataTree.hpp"

#include <filesystem>
#include <string>

namespace vsr::scivis_studio {

constexpr const char *PROJECT_KIND = "SciVisStudio";
constexpr const char *PROJECT_FILE_TYPE = "project";
constexpr const char *PROJECT_SCHEMA = "vsr.scivis-studio.project";
// v6: the manifest no longer embeds a residual Scene Archive. Camera and
// renderer pools live in required Archives under scene/.
constexpr int DECOMPOSED_SCENE_SCHEMA_VERSION = 6;
// v7: each manifest dataset records its residency (Loaded/Unloaded); an
// absent field means Loaded, so v6 projects behave identically.
// v8: a File Animation Dataset persists its source list only in a sibling
// Source List File (datasets/<name>.sources); dataset files no longer embed
// sourceFiles, though legacy embedded sourceFiles still load and migrate on
// the next explicit save (ADR 0013).
constexpr int SCHEMA_VERSION = 8;
constexpr const char *PROJECT_MANIFEST_FILENAME = "project.vsr";

// Projects written before the TSD -> VSR rename spell every Archive with a
// ".tsd" extension and a "project.tsd" manifest. Reads accept either spelling
// so an untouched legacy project directory still opens; saving always emits
// the current ".vsr" names, which migrates the directory on the next save.
constexpr const char *PROJECT_FILE_EXTENSION = ".vsr";
constexpr const char *LEGACY_PROJECT_FILE_EXTENSION = ".tsd";
constexpr const char *LEGACY_PROJECT_MANIFEST_FILENAME = "project.tsd";

// Returns `file` when it exists, otherwise its legacy ".tsd" spelling when
// that exists, otherwise `file` unchanged so callers report the current name
// in "missing file" diagnostics.
std::filesystem::path resolveProjectFileForRead(std::filesystem::path file);

// True when `extension` is one of the project Archive extensions accepted on
// read (".vsr" or the legacy ".tsd").
bool isProjectFileExtension(const std::filesystem::path &extension);

// Standalone rig Archive schemas, versioned independently of the project.
constexpr const char *CAMERA_RIG_FILE_TYPE = "camera-rig";
constexpr const char *CAMERA_RIG_SCHEMA = "vsr.scivis-studio.camerarig";
constexpr const char *LIGHT_RIG_FILE_TYPE = "light-rig";
constexpr const char *LIGHT_RIG_SCHEMA = "vsr.scivis-studio.lightrig";
constexpr int RIG_SCHEMA_VERSION = 1;

struct ProjectValidationResult
{
  bool ok{false};
  std::string error;
  std::filesystem::path manifestPath;
};

void projectToNode(const Project &project, vsr::core::DataNode &node);
bool nodeToProject(vsr::core::DataNode &node, Project &project);

// Rig names double as on-disk filenames ("<name>.vsr"), so they are restricted
// to a portable character set (letters, digits, space, '_', '-', '(', ')') with
// no leading/trailing whitespace. validateRigName checks a user-entered name's
// format only (not collection uniqueness); sanitizeRigName coerces an arbitrary
// string (e.g. a loaded rig's stored name) into that set.
bool validateRigName(const std::string &name, std::string *error = nullptr);
std::string sanitizeRigName(const std::string &name);

ProjectValidationResult validateProjectRoot(
    const std::filesystem::path &directory);

} // namespace vsr::scivis_studio
