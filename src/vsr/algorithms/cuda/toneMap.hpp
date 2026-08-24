// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cuda_runtime_api.h>
#include <cstdint>
#include "vsr/algorithms/cpu/toneMap.hpp" // ToneMapOperator enum

namespace vsr::algorithms::cuda {

void toneMap(cudaStream_t stream,
    float *hdrColor,
    uint32_t numPixels,
    float exposureScale,
    ToneMapOperator op);

void toneMap(float *hdrColor,
    uint32_t numPixels,
    float exposureScale,
    ToneMapOperator op);

} // namespace vsr::algorithms::cuda
