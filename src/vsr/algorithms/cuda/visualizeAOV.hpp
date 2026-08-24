// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cuda_runtime_api.h>
#include <cstdint>
#include "vsr/core/VSRMath.hpp"

namespace vsr::algorithms::cuda {

void visualizeId(cudaStream_t stream,
    const uint32_t *id,
    uint32_t *color,
    uint32_t width,
    uint32_t height);
void visualizeId(
    const uint32_t *id, uint32_t *color, uint32_t width, uint32_t height);

void visualizeDepth(cudaStream_t stream,
    const float *depth,
    uint32_t *color,
    float minDepth,
    float maxDepth,
    uint32_t width,
    uint32_t height);
void visualizeDepth(const float *depth,
    uint32_t *color,
    float minDepth,
    float maxDepth,
    uint32_t width,
    uint32_t height);

void visualizeAlbedo(cudaStream_t stream,
    const vsr::math::float3 *albedo,
    uint32_t *color,
    uint32_t width,
    uint32_t height);
void visualizeAlbedo(const vsr::math::float3 *albedo,
    uint32_t *color,
    uint32_t width,
    uint32_t height);

void visualizeNormal(cudaStream_t stream,
    const vsr::math::float3 *normal,
    uint32_t *color,
    uint32_t width,
    uint32_t height);
void visualizeNormal(const vsr::math::float3 *normal,
    uint32_t *color,
    uint32_t width,
    uint32_t height);

void visualizeEdges(cudaStream_t stream,
    const uint32_t *objectId,
    uint32_t *color,
    bool invert,
    uint32_t width,
    uint32_t height);
void visualizeEdges(const uint32_t *objectId,
    uint32_t *color,
    bool invert,
    uint32_t width,
    uint32_t height);

} // namespace vsr::algorithms::cuda
