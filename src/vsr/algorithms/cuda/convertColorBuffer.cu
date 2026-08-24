// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cuda/convertColorBuffer.hpp"
// thrust
#include <thrust/execution_policy.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>

namespace vsr::algorithms::cuda {

void convertFloatToUint8(
    cudaStream_t stream, const float *src, uint8_t *dst, size_t count)
{
  thrust::for_each(thrust::cuda::par.on(stream),
      thrust::make_counting_iterator(size_t(0)),
      thrust::make_counting_iterator(count),
      [=] __device__(size_t i) {
        dst[i] = uint8_t(fminf(fmaxf(src[i], 0.f), 1.f) * 255);
      });
}

void convertFloatToUint8(const float *src, uint8_t *dst, size_t count)
{
  convertFloatToUint8(cudaStream_t{0}, src, dst, count);
}

} // namespace vsr::algorithms::cuda
