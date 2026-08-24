// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cuda/clearBuffers.hpp"
// thrust
#include <thrust/execution_policy.h>
#include <thrust/fill.h>

namespace vsr::algorithms::cuda {

void fill(cudaStream_t stream, uint32_t *buf, uint32_t count, uint32_t value)
{
  thrust::fill(thrust::cuda::par.on(stream), buf, buf + count, value);
}

void fill(uint32_t *buf, uint32_t count, uint32_t value)
{
  fill(cudaStream_t{0}, buf, count, value);
}

void fill(cudaStream_t stream, float *buf, uint32_t count, float value)
{
  thrust::fill(thrust::cuda::par.on(stream), buf, buf + count, value);
}

void fill(float *buf, uint32_t count, float value)
{
  fill(cudaStream_t{0}, buf, count, value);
}

} // namespace vsr::algorithms::cuda
