// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cpu/outputTransform.hpp"
#include "../math/color.h"
#include "detail/parallel_for.h"
// helium
#include <helium/helium_math.h>

namespace vsr::algorithms::cpu {

void outputTransform(const float *hdrColor,
    const uint32_t *colorIn,
    uint32_t *colorOut,
    uint32_t numPixels,
    float invGamma,
    anari::DataType colorFormat)
{
  detail::parallel_for(0u, numPixels, [=](uint32_t i) {
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

} // namespace vsr::algorithms::cpu
