// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_algorithms
#include "vsr/algorithms/cpu/downsample.hpp"
#include "../math/color.h"
#include "detail/parallel_reduce.h"
// std
#include <algorithm>
#include <cmath>

namespace vsr::algorithms::cpu {

// Host sample budget. Cost is resolution-independent: a full-image reduction
// scans every pixel through a transcendental log2 each frame (~23 ms at 4K on
// a serial build), while striding to this many samples estimates the mean to
// within ~0.02 stops — invisible after the exp2 / clamp / temporal smoothing
// the auto-exposure pass applies. Images at or below the budget read fully.
static constexpr uint32_t SAMPLE_COUNT = 16384;

std::optional<float> meanLogLuminance(
    const float *hdrColor, uint32_t width, uint32_t height)
{
  if (width == 0 || height == 0 || !hdrColor)
    return {};

  // 64-bit: width*height overflows uint32_t at 2^32 texels (65536x65536 wraps
  // to exactly 0), which would silently report an empty image.
  const uint64_t total = uint64_t(width) * height;
  if (total > uint64_t(UINT32_MAX))
    return {};

  const uint32_t stride = std::max(1u, uint32_t(total) / SAMPLE_COUNT);
  const uint32_t numSamples = (uint32_t(total) + stride - 1) / stride;
  const float sum = detail::parallel_reduce(
      0u,
      numSamples,
      0.f,
      [=](uint32_t j) -> float {
        const size_t idx = size_t(j) * stride * 4;
        return std::log2(math::clampedLuminance(
            hdrColor[idx + 0], hdrColor[idx + 1], hdrColor[idx + 2]));
      },
      [](float a, float b) -> float { return a + b; });
  return sum / float(numSamples);
}

} // namespace vsr::algorithms::cpu
