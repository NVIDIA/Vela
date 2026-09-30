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

// Host sample budget. A full-image scan is memory-bound and grows with the
// image (milliseconds per frame at 4K+), while this many stratified samples
// estimate the mean well inside what the exp2 / clamp / temporal smoothing of
// the auto-exposure pass can show. Images at or below the budget are reduced
// exactly.
static constexpr uint32_t SAMPLE_BUDGET = 65536;

static constexpr uint32_t JITTER_BITS = 16;
static constexpr uint32_t JITTER_MASK = (1u << JITTER_BITS) - 1;

// Cells ahead to prefetch. Samples are scattered cache misses on frames
// larger than the LLC; prefetching keeps several in flight per thread.
static constexpr uint32_t PREFETCH_DISTANCE = 8;

// lowbias32 (C. Wellons): full avalanche, so neighbouring cells and
// consecutive seeds jitter independently.
static uint32_t hash(uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

struct CellGrid
{
  uint32_t x, y;
};

// About SAMPLE_BUDGET near-square cells. The short axis is sized first and
// the long axis gets the rest of the budget, so a thin image whose short axis
// clamps to one cell still stays within budget. Below 2^32 texels a cell
// spans at most 2^16 texels per axis (a 1-texel-thin image), so JITTER_BITS
// per axis always covers it.
static CellGrid cellGrid(uint32_t width, uint32_t height)
{
  const double cellsPerTexel =
      std::sqrt(double(SAMPLE_BUDGET) / (double(width) * height));
  const bool isWide = width >= height;
  const uint32_t shortExtent = isWide ? height : width;
  const uint32_t longExtent = isWide ? width : height;
  const uint32_t shortCells = std::clamp(
      uint32_t(std::lround(shortExtent * cellsPerTexel)), 1u, shortExtent);
  const uint32_t longCells =
      std::clamp(uint32_t(std::lround(double(SAMPLE_BUDGET) / shortCells)),
          1u,
          longExtent);
  return isWide ? CellGrid{longCells, shortCells}
                : CellGrid{shortCells, longCells};
}

// First texel of `cell` when `extent` texels split into `cells` cells;
// neighbouring cells differ in size by at most one texel.
static uint32_t cellStart(uint32_t cell, uint32_t cells, uint32_t extent)
{
  return uint32_t(uint64_t(cell) * extent / cells);
}

// Maps JITTER_BITS random bits onto [0, n) by multiply-shift (Lemire), no
// division on the per-sample path.
static uint32_t scaleJitter(uint32_t bits, uint32_t n)
{
  return uint32_t((uint64_t(bits) * n) >> JITTER_BITS);
}

// Texel index of `cell`'s jittered sample.
static size_t sampleTexel(uint32_t cell,
    uint32_t seedHash,
    uint32_t x0,
    uint32_t cellW,
    uint32_t y0,
    uint32_t cellH,
    uint32_t width)
{
  const uint32_t jitter = hash(cell ^ seedHash);
  const uint32_t x = x0 + scaleJitter(jitter & JITTER_MASK, cellW);
  const uint32_t y = y0 + scaleJitter(jitter >> JITTER_BITS, cellH);
  return size_t(y) * width + x;
}

// Only a hint: compiles away where the builtin is unavailable.
static void prefetch(const void *address)
{
#if defined(__GNUC__) || defined(__clang__)
  __builtin_prefetch(address);
#else
  (void)address;
#endif
}

std::optional<float> meanLogLuminance(
    const float *hdrColor, uint32_t width, uint32_t height, uint32_t seed)
{
  if (width == 0 || height == 0 || !hdrColor)
    return {};

  // 64-bit: width*height overflows uint32_t at 2^32 texels (65536x65536 wraps
  // to exactly 0), which would silently report an empty image.
  const uint64_t total = uint64_t(width) * height;
  if (total > uint64_t(UINT32_MAX))
    return {};

  // One jittered sample per cell, weighted by the cell's area: cell areas
  // sum to the texel count, so the estimate is unbiased, and 1-texel cells
  // make it exact. Rows of cells run in parallel; within a row, cell bounds
  // advance incrementally and the sample PREFETCH_DISTANCE cells ahead is
  // prefetched.
  const CellGrid grid = cellGrid(width, height);
  const uint32_t seedHash = hash(seed);
  const float sum = detail::parallel_reduce(
      0u,
      grid.y,
      0.f,
      [=](uint32_t cy) -> float {
        const uint32_t y0 = cellStart(cy, grid.y, height);
        const uint32_t cellH = cellStart(cy + 1, grid.y, height) - y0;
        const uint32_t rowFirstCell = cy * grid.x;
        const auto sampleOffset = [&](uint32_t cx,
                                      uint32_t x0,
                                      uint32_t cellW) {
          return sampleTexel(
                     rowFirstCell + cx, seedHash, x0, cellW, y0, cellH, width)
              * 4;
        };
        float rowSum = 0.f;
        uint32_t x0 = 0;
        for (uint32_t cx = 0; cx < grid.x; cx++) {
          const uint32_t ahead = cx + PREFETCH_DISTANCE;
          if (ahead < grid.x) {
            const uint32_t aheadX0 = cellStart(ahead, grid.x, width);
            const uint32_t aheadW =
                cellStart(ahead + 1, grid.x, width) - aheadX0;
            prefetch(hdrColor + sampleOffset(ahead, aheadX0, aheadW));
          }
          const uint32_t x1 = cellStart(cx + 1, grid.x, width);
          const uint32_t cellW = x1 - x0;
          const size_t idx = sampleOffset(cx, x0, cellW);
          rowSum += float(cellW * cellH)
              * std::log2(math::clampedLuminance(
                  hdrColor[idx + 0], hdrColor[idx + 1], hdrColor[idx + 2]));
          x0 = x1;
        }
        return rowSum;
      },
      [](float a, float b) -> float { return a + b; });
  return sum / float(total);
}

} // namespace vsr::algorithms::cpu
