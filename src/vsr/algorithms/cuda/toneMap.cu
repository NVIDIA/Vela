// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../math/tonemap_curves.h"
#include "vsr/algorithms/cuda/toneMap.hpp"
// thrust
#include <thrust/execution_policy.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>

namespace vsr::algorithms::cuda {

void toneMap(cudaStream_t stream,
    float *hdrColor,
    uint32_t numPixels,
    float exposureScale,
    ToneMapOperator op)
{
  thrust::for_each(thrust::cuda::par.on(stream),
      thrust::make_counting_iterator(0u),
      thrust::make_counting_iterator(numPixels),
      [=] __device__(uint32_t i) {
        const uint32_t idx = i * 4;
        vsr::math::float3 c(
            hdrColor[idx + 0], hdrColor[idx + 1], hdrColor[idx + 2]);
        const float alpha = hdrColor[idx + 3];

        c = c * exposureScale;

        switch (op) {
        case ToneMapOperator::NONE:
          break;
        case ToneMapOperator::REINHARD:
          c = math::tonemapReinhard(math::max0(c));
          break;
        case ToneMapOperator::ACES:
          c = math::tonemapACES(math::max0(c));
          break;
        case ToneMapOperator::HABLE:
          c = math::tonemapHable(math::max0(c));
          break;
        case ToneMapOperator::KHRONOS_PBR_NEUTRAL:
          c = math::tonemapKhronosPbrNeutral(math::max0(c));
          break;
        case ToneMapOperator::AGX:
          c = math::tonemapAgX(math::max0(c));
          break;
        }

        hdrColor[idx + 0] = c.x;
        hdrColor[idx + 1] = c.y;
        hdrColor[idx + 2] = c.z;
        hdrColor[idx + 3] = alpha;
      });
}

void toneMap(float *hdrColor,
    uint32_t numPixels,
    float exposureScale,
    ToneMapOperator op)
{
  toneMap(cudaStream_t{0}, hdrColor, numPixels, exposureScale, op);
}

} // namespace vsr::algorithms::cuda
