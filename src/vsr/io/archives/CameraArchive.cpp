// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/archives/CameraArchive.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_io
#include "vsr/io/archives/detail/PoolArchive.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"

namespace vsr::io {

bool serialize_CameraArchive(const scene::Scene &scene, core::DataNode &archive)
{
  return detail::serializePoolArchive(
      scene, archive, ANARI_CAMERA, "camera", schema::SCENE_CAMERAS);
}

ArchiveValidationResult validate_CameraArchive(core::DataNode &archive)
{
  return detail::validatePoolArchive(
      archive, ANARI_CAMERA, "camera", schema::SCENE_CAMERAS);
}

bool deserialize_CameraArchive(scene::Scene &scene,
    core::DataNode &archive,
    ArchiveValidationResult *validation)
{
  auto result = validate_CameraArchive(archive);
  if (validation)
    *validation = result;
  if (!result.accepted())
    return false;

  scene.m_defaultObjects.camera.reset();
  return detail::deserializePoolArchive(scene,
      archive,
      ANARI_CAMERA,
      "camera",
      schema::SCENE_CAMERAS,
      validation);
}

bool save_CameraArchive(const scene::Scene &scene, const char *filename)
{
  if (!filename)
    return false;
  core::DataTree tree;
  return serialize_CameraArchive(scene, tree.root()) && tree.save(filename);
}

bool load_CameraArchive(scene::Scene &scene,
    const char *filename,
    ArchiveValidationResult *validation)
{
  if (!filename)
    return false;
  core::DataTree tree;
  return tree.load(filename)
      && deserialize_CameraArchive(scene, tree.root(), validation);
}

} // namespace vsr::io
