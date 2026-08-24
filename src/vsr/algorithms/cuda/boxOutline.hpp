// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/VSRMath.hpp"
// cuda
#include <cuda_runtime_api.h>
// std
#include <cstdint>

namespace vsr::algorithms::cuda {

void boxOutline(cudaStream_t stream,
    const vsr::math::box3 &box,
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

} // namespace vsr::algorithms::cuda
