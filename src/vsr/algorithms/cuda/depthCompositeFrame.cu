// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cuda/depthCompositeFrame.hpp"
// thrust
#include <thrust/execution_policy.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>

namespace vsr::algorithms::cuda {

void depthCompositeFrame(cudaStream_t stream,
    uint32_t *outColor,
    float *outDepth,
    uint32_t *outObjectId,
    const uint32_t *inColor,
    const float *inDepth,
    const uint32_t *inObjectId,
    uint32_t pixelCount,
    bool firstPass)
{
  thrust::for_each(thrust::cuda::par.on(stream),
      thrust::make_counting_iterator(0u),
      thrust::make_counting_iterator(pixelCount),
      [=] __device__(uint32_t i) {
        const float currentDepth = inDepth[i];
        const float incomingDepth = outDepth[i];
        if (firstPass || currentDepth < incomingDepth) {
          outDepth[i] = currentDepth;
          outColor[i] = inColor[i];
          if (inObjectId)
            outObjectId[i] = inObjectId[i];
        }
      });
}

void depthCompositeFrame(uint32_t *outColor,
    float *outDepth,
    uint32_t *outObjectId,
    const uint32_t *inColor,
    const float *inDepth,
    const uint32_t *inObjectId,
    uint32_t pixelCount,
    bool firstPass)
{
  depthCompositeFrame(cudaStream_t{0},
      outColor,
      outDepth,
      outObjectId,
      inColor,
      inDepth,
      inObjectId,
      pixelCount,
      firstPass);
}

} // namespace vsr::algorithms::cuda
