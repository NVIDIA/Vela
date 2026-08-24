// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cuda_runtime_api.h>
#include <anari/anari_cpp.hpp>
#include <cstdint>

namespace vsr::algorithms::cuda {

void outputTransform(cudaStream_t stream,
    const float *hdrColor,
    const uint32_t *colorIn,
    uint32_t *colorOut,
    uint32_t numPixels,
    float invGamma,
    anari::DataType colorFormat);

void outputTransform(const float *hdrColor,
    const uint32_t *colorIn,
    uint32_t *colorOut,
    uint32_t numPixels,
    float invGamma,
    anari::DataType colorFormat);

} // namespace vsr::algorithms::cuda
