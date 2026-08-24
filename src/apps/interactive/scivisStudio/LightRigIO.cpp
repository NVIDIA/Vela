// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LightRigIO.h"

#include "ProjectSerialization.h"

#include "vsr/core/DataTree.hpp"
#include "vsr/io/archives/SubtreeArchiveContent.hpp"
#include "vsr/scene/Scene.hpp"

namespace vsr::scivis_studio {

namespace {

const vsr::io::SubtreeArchiveContentDesc LIGHT_RIG_ARCHIVE_DESC{
    LIGHT_RIG_FILE_TYPE,
    LIGHT_RIG_SCHEMA,
    vsr::io::ArchiveObjectPolicy::LightsOnly};

} // namespace

vsr::io::ArchiveValidationResult validateLightRigArchive(
    vsr::core::DataNode &archive)
{
  return vsr::io::validate_SubtreeArchiveContent(
      archive, LIGHT_RIG_ARCHIVE_DESC);
}

bool saveLightRigArchiveFile(vsr::scene::LayerNodeRef root,
    const std::filesystem::path &file,
    std::string_view displayName)
{
  vsr::core::DataTree tree;
  return vsr::io::serialize_SubtreeArchiveContent(
             root, tree.root(), LIGHT_RIG_ARCHIVE_DESC, displayName)
      && tree.save(file.string().c_str());
}

vsr::scene::LayerNodeRef deserializeLightRigArchive(vsr::scene::Scene &scene,
    vsr::core::DataNode &archive,
    vsr::scene::LayerNodeRef destination,
    std::string *displayName)
{
  return vsr::io::deserialize_SubtreeArchiveContent(
      scene, archive, destination, LIGHT_RIG_ARCHIVE_DESC, displayName)
      .root;
}

vsr::scene::LayerNodeRef loadLightRigArchiveFile(vsr::scene::Scene &scene,
    const std::filesystem::path &file,
    vsr::scene::LayerNodeRef destination,
    std::string *displayName)
{
  vsr::core::DataTree tree;
  if (!tree.load(file.string().c_str()))
    return {};
  return deserializeLightRigArchive(
      scene, tree.root(), destination, displayName);
}

} // namespace vsr::scivis_studio
