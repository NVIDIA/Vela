// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cuda_runtime_api.h>
#include <cstdint>

namespace vsr::algorithms::cuda {

float sumLogLuminance(cudaStream_t stream,
    const float *hdrColor,
    uint32_t numSamples,
    uint32_t stride);

float sumLogLuminance(
    const float *hdrColor, uint32_t numSamples, uint32_t stride);

} // namespace vsr::algorithms::cuda
