// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Scene.hpp"
#include "vsr/io/procedural/computeVorticity.hpp"

namespace vsr::io {

using namespace vsr::scene;

// clang-format off

void generate_cylinders(Scene &scene, LayerNodeRef location = {}, bool useDefaultMaterial = false);
void generate_default_lights(Scene &scene);
void generate_emissive_geometries(Scene &scene, LayerNodeRef location = {});
void generate_emissive_materialx_comparison(Scene &scene, LayerNodeRef location = {});
void generate_emissive_mdl_comparison(Scene &scene, LayerNodeRef location = {});
void generate_hdri_dome(Scene &scene, LayerNodeRef location = {});
void generate_hdri_test_image(Scene &scene, LayerNodeRef location = {});
void generate_icosphere(Scene &scene, LayerNodeRef location = {}, uint32_t subdivisions = 4, bool withFloor = true);
void generate_monkey(Scene &scene, LayerNodeRef location = {});
VolumeRef generate_noiseVolume(Scene &scene, LayerNodeRef location = {}, ArrayRef colors = {}, ArrayRef opacities = {});
void generate_randomSpheres(Scene &scene, LayerNodeRef location = {}, bool useDefaultMaterial = false);
void generate_rtow(Scene &scene, LayerNodeRef location = {});
void generate_sphereSetVolume(Scene &scene, LayerNodeRef location = {});

// clang-format on

} // namespace vsr::io
