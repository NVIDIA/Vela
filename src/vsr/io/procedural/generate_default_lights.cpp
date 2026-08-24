// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/procedural.hpp"

namespace vsr::io {

void generate_default_lights(Scene &scene)
{
  auto *layer = scene.defaultLayer();
  auto lightsRoot = layer->root()->insert_first_child({layer, "defaultLights"});

  auto light = scene.createObject<vsr::scene::Light>(
      vsr::scene::tokens::light::directional);
  light->setName("mainDistantLight");
  light->setParameter("direction", vsr::math::float2(0.f, 240.f));

  scene.insertChildObjectNode(lightsRoot, light);
}

} // namespace vsr::io
