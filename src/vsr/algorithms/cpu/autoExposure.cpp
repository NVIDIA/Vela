// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cpu/autoExposure.hpp"
#include "../math/color.h"
#include "detail/parallel_reduce.h"
// std
#include <algorithm>
#include <cmath>

namespace vsr::algorithms::cpu {

static constexpr float MIN_LUMINANCE = 1e-4f;

float sumLogLuminance(
    const float *hdrColor, uint32_t numSamples, uint32_t stride)
{
  const float minLum = MIN_LUMINANCE;
  return detail::parallel_reduce(
      0u,
      numSamples,
      0.f,
      [=](uint32_t j) -> float {
        const uint32_t idx = j * stride * 4;
        const float lum = std::max(
            math::luminance(
                hdrColor[idx + 0], hdrColor[idx + 1], hdrColor[idx + 2]),
            minLum);
        return std::log2(lum);
      },
      [](float a, float b) -> float { return a + b; });
}

} // namespace vsr::algorithms::cpu
