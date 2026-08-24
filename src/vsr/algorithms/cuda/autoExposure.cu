// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../math/color.h"
#include "vsr/algorithms/cuda/autoExposure.hpp"
// thrust
#include <thrust/execution_policy.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/transform_reduce.h>

namespace vsr::algorithms::cuda {

static constexpr float MIN_LUMINANCE = 1e-4f;

float sumLogLuminance(cudaStream_t stream,
    const float *hdrColor,
    uint32_t numSamples,
    uint32_t stride)
{
  const float minLum = MIN_LUMINANCE;
  return thrust::transform_reduce(
      thrust::cuda::par.on(stream),
      thrust::make_counting_iterator(0u),
      thrust::make_counting_iterator(numSamples),
      [=] __host__ __device__(uint32_t j) -> float {
        const uint32_t idx = j * stride * 4;
        const float lum =
            max(math::luminance(
                    hdrColor[idx + 0], hdrColor[idx + 1], hdrColor[idx + 2]),
                minLum);
        return log2f(lum);
      },
      0.f,
      [] __host__ __device__(float a, float b) -> float { return a + b; });
}

float sumLogLuminance(
    const float *hdrColor, uint32_t numSamples, uint32_t stride)
{
  return sumLogLuminance(cudaStream_t{0}, hdrColor, numSamples, stride);
}

} // namespace vsr::algorithms::cuda
