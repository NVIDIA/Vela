// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/pick/PickRequest.h"
// std
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsr::rendering {

std::optional<PickHit> pick(PickableSource &source, const PickRequest &request)
{
  const auto size = source.pickImageSize();
  if (size.x == 0 || size.y == 0)
    return {};

  const vsr::math::uint2 pixel(std::min(request.pixel.x, size.x - 1),
      std::min(request.pixel.y, size.y - 1));

  const auto sample = source.renderPickSample(pixel);
  if (!sample)
    return {};

  PickHit hit;
  hit.depth = sample->depth;
  hit.object = vsr::scene::decodeObjectId(sample->objectId);
  hit.instanceId = sample->instanceId;
  hit.primitiveId = sample->primitiveId;

  // Devices clear depth to infinity or to the largest float.
  const bool hasDepth = hit.depth < std::numeric_limits<float>::max();
  if (!hasDepth)
    hit.depth = vsr::math::inf;
  if (!hasDepth && !hit.object)
    return {};

  if (hasDepth && request.view) {
    const vsr::math::float2 screen(
        (pixel.x + 0.5f) / size.x, (pixel.y + 0.5f) / size.y);
    const auto ray = primaryRay(*request.view, screen, size.x / float(size.y));
    hit.position = ray.origin + hit.depth * ray.direction;
  }

  return hit;
}

} // namespace vsr::rendering
