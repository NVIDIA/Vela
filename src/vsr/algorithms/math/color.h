// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "device_macros.h"
#include "vsr/core/VSRMath.hpp"
// std
#include <algorithm>
#include <cmath>

namespace vsr::algorithms::math {

VSR_HOST_DEVICE_FCN inline float luminance(float r, float g, float b)
{
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Luminance bounds for log-domain reductions. The floor keeps log2 finite on
// black texels; the ceiling keeps a single +Inf or overbright firefly from
// dominating a full-image mean (log2(1e8) ~= 26.6, comfortably outside the
// +/-20-stop exposure clamp, so in-range content is never touched).
static constexpr float MIN_LUMINANCE = 1e-4f;
static constexpr float MAX_LUMINANCE = 1e8f;

// Luminance clamped into [MIN_LUMINANCE, MAX_LUMINANCE], with NaN mapped to
// MIN_LUMINANCE (a NaN texel reads as black rather than poisoning the whole
// reduction).
//
// The comparisons are spelled out rather than delegated to std::max/fmaxf on
// purpose: those two disagree about which argument wins when one is NaN, and
// the CPU and CUDA backends have to agree texel for texel for the mirrored
// algorithms to return the same result (STYLEGUIDE Sec. 20). Writing the
// bounds as `!(lum > MIN)` / `!(lum < MAX)` makes every NaN comparison fail,
// which lands NaN on the floor by construction.
VSR_HOST_DEVICE_FCN inline float clampedLuminance(float r, float g, float b)
{
  const float lum = luminance(r, g, b);
  if (!(lum > MIN_LUMINANCE))
    return MIN_LUMINANCE;
  if (!(lum < MAX_LUMINANCE))
    return MAX_LUMINANCE;
  return lum;
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 linearToGamma(
    vsr::math::float3 c, float invGamma)
{
  c.x = std::pow(std::clamp(c.x, 0.f, 1.f), invGamma);
  c.y = std::pow(std::clamp(c.y, 0.f, 1.f), invGamma);
  c.z = std::pow(std::clamp(c.z, 0.f, 1.f), invGamma);
  return c;
}

// Deterministic pseudo-random color from an ID (same as visrtx gpu_utils.h)
VSR_DEVICE_FCN inline vsr::math::float3 makeRandomColor(uint32_t i)
{
  const uint32_t mx = 13 * 17 * 43;
  const uint32_t my = 11 * 29;
  const uint32_t mz = 7 * 23 * 63;
  const uint32_t g = (i * (3 * 5 * 127) + 12312314);

  if (i == ~0u)
    return vsr::math::float3(0.0f);

  return vsr::math::float3((g % mx) * (1.f / (mx - 1)),
      (g % my) * (1.f / (my - 1)),
      (g % mz) * (1.f / (mz - 1)));
}

} // namespace vsr::algorithms::math
