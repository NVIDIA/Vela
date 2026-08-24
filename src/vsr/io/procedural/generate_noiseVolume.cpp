// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/io/procedural.hpp"
// std
#include <algorithm>
#include <random>

namespace vsr::io {

VolumeRef generate_noiseVolume(Scene &scene,
    LayerNodeRef location,
    ArrayRef colorArray,
    ArrayRef opacityArray)
{
  if (!location)
    location = scene.defaultLayer()->root();

  // Generate spatial field //

  static std::mt19937 rng;
  rng.seed(0);
  static std::normal_distribution<float> dist(0.f, 1.0f);

  auto field = scene.createObject<SpatialField>(
      tokens::spatial_field::structuredRegular);
  field->setName("noise_field");

  auto voxelArray = scene.createArray(ANARI_UFIXED8, 64, 64, 64);

  auto *voxelsBegin = (uint8_t *)voxelArray->map();
  auto *voxelsEnd = voxelsBegin + (64 * 64 * 64);

  std::for_each(voxelsBegin, voxelsEnd, [&](auto &v) { v = dist(rng) * 255; });

  voxelArray->unmap();

  field->setParameter("origin", float3(-1, -1, -1));
  field->setParameterObject("data", *voxelArray);

  // Setup volume //

  auto [inst, volume] = scene.insertNewChildObjectNode<Volume>(
      location, tokens::volume::transferFunction1D);
  volume->setName("noise_volume");

  if (!colorArray) {
    colorArray = scene.createArray(ANARI_FLOAT32_VEC4, 256);
    colorArray->setData(makeDefaultColorMap(colorArray->size()).data());
  }

  volume->setParameterObject("color", *colorArray);
  volume->setParameterObject("value", *field);

  return volume;
}

} // namespace vsr::io
