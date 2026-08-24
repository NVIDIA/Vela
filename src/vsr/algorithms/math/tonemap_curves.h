// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "device_macros.h"
#include "vsr/core/VSRMath.hpp"
// std
#include <algorithm>
#include <cmath>

namespace vsr::algorithms::math {

VSR_DEVICE_FCN_INLINE vsr::math::float3 max0(vsr::math::float3 c)
{
  return {std::max(c.x, 0.f), std::max(c.y, 0.f), std::max(c.z, 0.f)};
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 tonemapReinhard(vsr::math::float3 c)
{
  return c / (c + 1.f);
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 tonemapACES(vsr::math::float3 c)
{
  // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
  const float a = 2.51f;
  const float b = 0.03f;
  const float cc = 2.43f;
  const float d = 0.59f;
  const float e = 0.14f;
  return (c * (a * c + b)) / (c * (cc * c + d) + e);
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 hablePartial(vsr::math::float3 x)
{
  const float A = 0.15f;
  const float B = 0.50f;
  const float C = 0.10f;
  const float D = 0.20f;
  const float E = 0.02f;
  const float F = 0.30f;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 tonemapHable(vsr::math::float3 c)
{
  const float W = 11.2f;
  auto whiteScale = vsr::math::float3(1.f) / hablePartial(vsr::math::float3(W));
  return hablePartial(c * 2.f) * whiteScale;
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 tonemapKhronosPbrNeutral(
    vsr::math::float3 c)
{
  // https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
  const float startCompression = 0.8f - 0.04f;
  const float desaturation = 0.15f;

  float x = std::min(c.x, std::min(c.y, c.z));
  float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
  c.x -= offset;
  c.y -= offset;
  c.z -= offset;

  float peak = std::max(c.x, std::max(c.y, c.z));
  if (peak < startCompression)
    return c;

  const float d2 = 1.f - startCompression;
  float newPeak = 1.f - d2 * d2 / (peak + d2 - startCompression);
  float scale = newPeak / peak;
  c.x *= scale;
  c.y *= scale;
  c.z *= scale;

  float g = 1.f - 1.f / (desaturation * (peak - newPeak) + 1.f);
  c.x = c.x + (newPeak - c.x) * g;
  c.y = c.y + (newPeak - c.y) * g;
  c.z = c.z + (newPeak - c.z) * g;
  return c;
}

VSR_DEVICE_FCN_INLINE vsr::math::float3 tonemapAgX(vsr::math::float3 c)
{
  // AgX base contrast (Troy Sobotka)
  vsr::math::float3 agx;
  agx.x = 0.842479062253094f * c.x + 0.0784335999999992f * c.y
      + 0.0792237451477643f * c.z;
  agx.y = 0.0423282422610123f * c.x + 0.878468636469772f * c.y
      + 0.0791661274605434f * c.z;
  agx.z = 0.0423756549057051f * c.x + 0.0784336000000001f * c.y
      + 0.879142973793104f * c.z;

  const float minEv = -12.47393f;
  const float maxEv = 4.026069f;
  const float range = maxEv - minEv;
  agx.x = std::clamp(
      (std::log2(std::max(agx.x, 1e-10f)) - minEv) / range, 0.f, 1.f);
  agx.y = std::clamp(
      (std::log2(std::max(agx.y, 1e-10f)) - minEv) / range, 0.f, 1.f);
  agx.z = std::clamp(
      (std::log2(std::max(agx.z, 1e-10f)) - minEv) / range, 0.f, 1.f);

  auto x2 = agx * agx;
  auto x4 = x2 * x2;
  agx = 15.5f * x4 * x2 - 40.14f * x4 * agx + 31.96f * x4 - 6.868f * x2 * agx
      + 0.4298f * x2 + 0.1191f * agx - 0.00232f;

  return {std::clamp(1.19687900512017f * agx.x - 0.0980208811401368f * agx.y
                  - 0.0990434085532547f * agx.z,
              0.f,
              1.f),
      std::clamp(-0.0528968517574562f * agx.x + 1.15190312990417f * agx.y
              - 0.0989611768448433f * agx.z,
          0.f,
          1.f),
      std::clamp(-0.0529716355144438f * agx.x - 0.0980434800157286f * agx.y
              + 1.15107367305587f * agx.z,
          0.f,
          1.f)};
}

} // namespace vsr::algorithms::math
