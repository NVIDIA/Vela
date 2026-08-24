// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/procedural/detail/checkerboard.hpp"

namespace vsr::io::detail {

SamplerRef makeCheckerboardTexture(Scene &scene, int size)
{
  auto tex = scene.createObject<Sampler>(tokens::sampler::image2D);

  auto array = scene.createArray(ANARI_FLOAT32_VEC4, size, size);
  auto *data = array->mapAs<vsr::math::float4>();

  constexpr auto lightGray = vsr::math::float4(.7f, .7f, .7f, 1.f);
  constexpr auto darkGray = vsr::math::float4(.3f, .3f, .3f, 1.f);
  for (int h = 0; h < size; h++) {
    for (int w = 0; w < size; w++) {
      bool even = h & 1;
      if (even)
        data[h * size + w] = w & 1 ? lightGray : darkGray;
      else
        data[h * size + w] = w & 1 ? darkGray : lightGray;
    }
  }
  array->unmap();

  tex->setParameterObject("image", *array);
  tex->setParameter("inAttribute", "attribute0");
  tex->setParameter("wrapMode1", "clampToEdge");
  tex->setParameter("wrapMode2", "clampToEdge");
  tex->setParameter("filter", "nearest");
  tex->setName("checkerboard");

  return tex;
}

} // namespace vsr::io::detail
