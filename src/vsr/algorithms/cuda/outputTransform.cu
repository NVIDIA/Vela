// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../math/color.h"
#include "vsr/algorithms/cuda/outputTransform.hpp"
// helium
#include <helium/helium_math.h>
// thrust
#include <thrust/execution_policy.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>

namespace vsr::algorithms::cuda {

void outputTransform(cudaStream_t stream,
    const float *hdrColor,
    const uint32_t *colorIn,
    uint32_t *colorOut,
    uint32_t numPixels,
    float invGamma,
    anari::DataType colorFormat)
{
  thrust::for_each(thrust::cuda::par.on(stream),
      thrust::make_counting_iterator(0u),
      thrust::make_counting_iterator(numPixels),
      [=] __device__(uint32_t i) {
        vsr::math::float4 c(0.f);

        if (colorFormat == ANARI_FLOAT32_VEC4) {
          const uint32_t idx = i * 4;
          c.x = hdrColor[idx + 0];
          c.y = hdrColor[idx + 1];
          c.z = hdrColor[idx + 2];
          c.w = hdrColor[idx + 3];
        } else {
          c = helium::cvt_color_to_float4(colorIn[i]);
        }

        const auto encoded =
            math::linearToGamma(vsr::math::float3(c.x, c.y, c.z), invGamma);
        colorOut[i] =
            helium::cvt_color_to_uint32({encoded.x, encoded.y, encoded.z, c.w});
      });
}

void outputTransform(const float *hdrColor,
    const uint32_t *colorIn,
    uint32_t *colorOut,
    uint32_t numPixels,
    float invGamma,
    anari::DataType colorFormat)
{
  outputTransform(cudaStream_t{0},
      hdrColor,
      colorIn,
      colorOut,
      numPixels,
      invGamma,
      colorFormat);
}

} // namespace vsr::algorithms::cuda
