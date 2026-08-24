// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cpu/depthCompositeFrame.hpp"
// detail
#include "detail/parallel_for.h"

namespace vsr::algorithms::cpu {

void depthCompositeFrame(uint32_t *outColor,
    float *outDepth,
    uint32_t *outObjectId,
    const uint32_t *inColor,
    const float *inDepth,
    const uint32_t *inObjectId,
    uint32_t pixelCount,
    bool firstPass)
{
  detail::parallel_for(0u, pixelCount, [=](uint32_t i) {
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

} // namespace vsr::algorithms::cpu
