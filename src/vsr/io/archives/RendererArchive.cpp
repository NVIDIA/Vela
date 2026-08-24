// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/archives/RendererArchive.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_io
#include "vsr/io/archives/detail/PoolArchive.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"

namespace vsr::io {

bool serialize_RendererArchive(
    const scene::Scene &scene, core::DataNode &archive)
{
  return detail::serializePoolArchive(
      scene, archive, ANARI_RENDERER, "renderer", schema::SCENE_RENDERERS);
}

ArchiveValidationResult validate_RendererArchive(core::DataNode &archive)
{
  return detail::validatePoolArchive(
      archive, ANARI_RENDERER, "renderer", schema::SCENE_RENDERERS);
}

bool deserialize_RendererArchive(scene::Scene &scene,
    core::DataNode &archive,
    ArchiveValidationResult *validation)
{
  return detail::deserializePoolArchive(scene,
      archive,
      ANARI_RENDERER,
      "renderer",
      schema::SCENE_RENDERERS,
      validation);
}

bool save_RendererArchive(const scene::Scene &scene, const char *filename)
{
  if (!filename)
    return false;
  core::DataTree tree;
  return serialize_RendererArchive(scene, tree.root()) && tree.save(filename);
}

bool load_RendererArchive(scene::Scene &scene,
    const char *filename,
    ArchiveValidationResult *validation)
{
  if (!filename)
    return false;
  core::DataTree tree;
  return tree.load(filename)
      && deserialize_RendererArchive(scene, tree.root(), validation);
}

} // namespace vsr::io
