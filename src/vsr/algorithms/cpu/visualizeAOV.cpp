// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cpu/visualizeAOV.hpp"
#include "../math/color.h"
#include "detail/parallel_for.h"
// helium
#include <helium/helium_math.h>
// std
#include <algorithm>

namespace vsr::algorithms::cpu {

void visualizeId(
    const uint32_t *id, uint32_t *color, uint32_t width, uint32_t height)
{
  detail::parallel_for(0u, width * height, [=](uint32_t i) {
    uint32_t v = id ? id[i] : ~0u;
    auto c = math::makeRandomColor(v);
    color[i] = helium::cvt_color_to_uint32({c, 1.f});
  });
}

void visualizeDepth(const float *depth,
    uint32_t *color,
    float minDepth,
    float maxDepth,
    uint32_t width,
    uint32_t height)
{
  detail::parallel_for(0u, width * height, [=](uint32_t i) {
    const float d = depth ? depth[i] : 0.f;
    const float range = maxDepth - minDepth;
    const float v =
        range > 0.f ? std::clamp((d - minDepth) / range, 0.f, 1.f) : 0.f;
    color[i] = helium::cvt_color_to_uint32({vsr::math::float3(v), 1.f});
  });
}

void visualizeAlbedo(const vsr::math::float3 *albedo,
    uint32_t *color,
    uint32_t width,
    uint32_t height)
{
  detail::parallel_for(0u, width * height, [=](uint32_t i) {
    const auto a = albedo ? albedo[i] : vsr::math::float3(0.f);
    color[i] = helium::cvt_color_to_uint32_srgb({a, 1.f});
  });
}

void visualizeNormal(const vsr::math::float3 *normal,
    uint32_t *color,
    uint32_t width,
    uint32_t height)
{
  detail::parallel_for(0u, width * height, [=](uint32_t i) {
    const auto n = normal ? normal[i] : vsr::math::float3(0.f);
    const auto visualNormal = (n + 1.f) * 0.5f;
    color[i] = helium::cvt_color_to_uint32({visualNormal, 1.f});
  });
}

void visualizeEdges(const uint32_t *objectId,
    uint32_t *color,
    bool invert,
    uint32_t width,
    uint32_t height)
{
  detail::parallel_for(0u, width * height, [=](uint32_t i) {
    uint32_t y = i / width;
    uint32_t x = i % width;

    uint32_t centerID = objectId ? objectId[i] : ~0u;

    if (centerID == ~0u) {
      color[i] = helium::cvt_color_to_uint32({vsr::math::float3(0.f), 1.f});
      return;
    }

    bool isEdge = false;

    for (int dy = -1; dy <= 1 && !isEdge; ++dy) {
      for (int dx = -1; dx <= 1 && !isEdge; ++dx) {
        if (dx == 0 && dy == 0)
          continue;

        int nx = static_cast<int>(x) + dx;
        int ny = static_cast<int>(y) + dy;

        if (nx >= 0 && nx < static_cast<int>(width) && ny >= 0
            && ny < static_cast<int>(height)) {
          size_t neighborIdx =
              static_cast<size_t>(nx) + static_cast<size_t>(ny) * width;
          uint32_t neighborID = objectId ? objectId[neighborIdx] : ~0u;

          if (centerID != neighborID)
            isEdge = true;
        }
      }
    }

    float edgeValue = isEdge ? 1.f : 0.f;
    if (invert)
      edgeValue = 1.f - edgeValue;

    color[i] = helium::cvt_color_to_uint32({vsr::math::float3(edgeValue), 1.f});
  });
}

} // namespace vsr::algorithms::cpu
