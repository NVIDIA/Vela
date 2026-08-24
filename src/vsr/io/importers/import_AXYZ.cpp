// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers.hpp"

#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
#include "vsr/scene/algorithms/computeScalarRange.hpp"
// std
#include <algorithm>
#include <cstdio>
#include <vector>

namespace vsr::io {

using namespace vsr::core;

void import_AXYZ(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filepath,
    LayerNodeRef location)
{
  std::string file = fileOf(filepath);

  // load particle data from file //

  auto *fp = std::fopen(filepath, "rb");
  if (!fp) {
    vsr::core::logError("[import_axyz] could not open file %s", filepath);
    return;
  }

  uint64_t numTimeSteps = 0;
  uint64_t numParticles = 0;
  auto r = std::fread(&numTimeSteps, sizeof(numTimeSteps), 1, fp);
  r = std::fread(&numParticles, sizeof(numParticles), 1, fp);

  if (numTimeSteps == 0 || numParticles == 0) {
    vsr::core::logError(
        "[import_axyz] animation has no points in '%s'", filepath);
    std::fclose(fp);
    return;
  }

  vsr::core::logInfo(
      "[import_axyz] loading [%zu] time steps containing [%zu]"
      " points each from %s",
      numTimeSteps,
      numParticles,
      filepath);

  std::vector<vsr::scene::ObjectUsePtr<vsr::scene::Array>> timeSteps;

  for (int t = 0; t < numTimeSteps; ++t) {
    auto positionsArray = scene.createArray(ANARI_FLOAT32_VEC3, numParticles);
    positionsArray->setName(
        ("vertex.position_" + file + '_' + std::to_string(t)).c_str());
    positionsArray->setData(fp);
    timeSteps.emplace_back(positionsArray);
  }

  std::fclose(fp);

  // create VSR objects //

  auto axyz_root = scene.insertChildTransformNode(
      location ? location : scene.defaultLayer()->root(),
      math::IDENTITY_MAT4,
      ("axyz_transform_" + file).c_str());

  // geometry + material

  auto geom = scene.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  geom->setName(("axyz_geometry" + file).c_str());
  geom->setParameter("radius", 0.1f); // TODO: something smarter
  geom->setParameterObject("vertex.position", *timeSteps[0]);

  auto mat = scene.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  mat->setName("axyz_material");
  mat->setParameter("color", vsr::math::float3(0.8f, 0.8f, 0.8f));

  // surface

  auto surface = scene.createSurface("axyz_surface", geom, mat);
  scene.insertChildObjectNode(axyz_root, surface);

  // animation

  auto tb = makeLinearTimeBase(timeSteps.size());
  auto &anim = animMgr.addAnimation(file.c_str());
  addArrayTimeStepBindings(
      anim, geom.data(), {Token("vertex.position")}, {timeSteps}, tb);
}

} // namespace vsr::io
