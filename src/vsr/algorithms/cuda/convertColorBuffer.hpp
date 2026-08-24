// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cuda_runtime_api.h>
#include <cstddef>
#include <cstdint>

namespace vsr::algorithms::cuda {

void convertFloatToUint8(
    cudaStream_t stream, const float *src, uint8_t *dst, size_t count);
void convertFloatToUint8(const float *src, uint8_t *dst, size_t count);

} // namespace vsr::algorithms::cuda
