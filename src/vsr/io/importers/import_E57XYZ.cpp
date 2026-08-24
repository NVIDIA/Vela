// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers.hpp"

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

void import_E57XYZ(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filepath,
    LayerNodeRef location)
{
  (void)animMgr;
  std::string file = fileOf(filepath);

  // load particle data from file //

  auto *fp = std::fopen(filepath, "rb");
  if (!fp) {
    logError("[import_e57xyz] could not open file %s", filepath);
    return;
  }

  uint64_t numParticles = 0;
  auto r = std::fread(&numParticles, sizeof(numParticles), 1, fp);

  logInfo(
      "[import_e57xyz] loading %zu points from %s", numParticles, file.c_str());

  std::vector<float3> positions(numParticles);
  std::vector<float3> colors(numParticles);
  r = std::fread(positions.data(), sizeof(float3), numParticles, fp);
  r = std::fread(colors.data(), sizeof(float3), numParticles, fp);
  std::fclose(fp);

  for (auto &c : colors)
    c = vsr::core::math::normalizeColor(c);

  // create VSR objects //

  auto xyz_root = scene.insertChildNode(
      location ? location : scene.defaultLayer()->root(), file.c_str());

  auto positionsArray = scene.createArray(ANARI_FLOAT32_VEC3, numParticles);
  auto colorsArray = scene.createArray(ANARI_FLOAT32_VEC3, numParticles);

  positionsArray->setData(positions);
  colorsArray->setData(colors);

  // geometry + material

  auto geom = scene.createObject<Geometry>(tokens::geometry::sphere);
  geom->setName("e57xyz_geometry");
  geom->setParameter("radius", 0.001f); // TODO: something smarter
  geom->setParameterObject("vertex.position", *positionsArray);
  geom->setParameterObject("vertex.color", *colorsArray);

  auto mat = scene.createObject<Material>(tokens::material::matte);
  mat->setName("e57xyz_material");
  mat->setParameter("color", "color");

  // surface

  auto surface = scene.createSurface("e57xyz_surface", geom, mat);
  scene.insertChildObjectNode(xyz_root, surface);
}

} // namespace vsr::io
