// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/VSRMath.hpp"
// std
#include <cstdint>

namespace vsr::algorithms::cpu {

// Rasterize the Box Outline (12 edges) of a world-space AABB into the color
// buffer. Corners are projected through projView; edge segments behind the
// eye are clipped (perspective). Fragments are depth-tested against 'depth'
// (eye-to-hit distance, with a small relative bias toward the camera) when
// non-null, otherwise drawn as an overlay. When orthographicDepth is true the
// fragment depth is the signed distance along 'dir' from the camera plane at
// 'eye' instead of the Euclidean distance from 'eye'.
void boxOutline(const vsr::math::box3 &box,
    const vsr::math::mat4 &projView,
    const vsr::math::float3 &eye,
    const vsr::math::float3 &dir,
    bool orthographicDepth,
    const float *depth,
    uint32_t *color,
    uint32_t outlineColor,
    uint32_t lineWidth,
    uint32_t width,
    uint32_t height);

} // namespace vsr::algorithms::cpu
