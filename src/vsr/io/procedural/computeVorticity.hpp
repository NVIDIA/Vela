// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/SpatialField.hpp"
#include "vsr/scene/objects/Volume.hpp"

namespace vsr::io {

using namespace vsr::scene;

struct VorticityOptions
{
  bool lambda2{true};
  bool qCriterion{true};
  bool vorticity{true};
  bool helicity{false};
};

struct VorticityResult
{
  VolumeRef lambda2;
  VolumeRef qCriterion;
  VolumeRef vorticity;
  VolumeRef helicity;
};

VorticityResult computeVorticity(Scene &scene,
    const SpatialField *u,
    const SpatialField *v,
    const SpatialField *w,
    LayerNodeRef location = {},
    VorticityOptions opts = {});

} // namespace vsr::io
