// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cuda_runtime_api.h>
#include <cstdint>

namespace vsr::algorithms::cuda {

void fill(cudaStream_t stream, uint32_t *buf, uint32_t count, uint32_t value);
void fill(uint32_t *buf, uint32_t count, uint32_t value);

void fill(cudaStream_t stream, float *buf, uint32_t count, float value);
void fill(float *buf, uint32_t count, float value);

} // namespace vsr::algorithms::cuda
