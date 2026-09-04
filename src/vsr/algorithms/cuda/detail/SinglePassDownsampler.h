// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// SPD-inspired CUDA mip downsampler (after AMD FidelityFX Single Pass
// Downsampler). One kernel launch reduces a source through up to 12 mip
// levels: each 256-thread block folds a 64x64 source tile through 6 levels
// in shared memory, then the last-finishing block (global atomic counter)
// folds the resulting <=64x64 level through 6 more. Sources larger than
// 4096x4096 loop additional launches on the host — the API stays one call.
//
// Self-contained on purpose (single header, no VSR dependencies) so it can be
// reused by other CUDA targets in this repository that share no build target
// with vsr_algorithms.
//
// Template hooks:
//   T    — reduced value type (float, float2, ...)
//   LOAD — T operator()(uint32_t x, uint32_t y) const; source fetch+map for
//          level 0 (out-of-range coordinates are never requested; edge tiles
//          clamp before calling)
//   OP   — T operator()(T, T, T, T) const; the 2x2 reduction (sum,
//          min/max, ...). Non-pow2 edges pad per PadMode: ClampEdge is
//          exact for min/max; Identity keeps additive reductions exact.

#pragma once

#include <cuda_runtime_api.h>

#include <cstdint>

namespace spd {

constexpr int SPD_MAX_LEVELS = 16; // enough for 65536x65536 sources
constexpr int SPD_LEVELS_PER_PASS = 6;
constexpr uint32_t SPD_TILE = 64;

template <typename T>
struct MipChainView
{
  T *level[SPD_MAX_LEVELS] = {};
  uint2 dims[SPD_MAX_LEVELS] = {};
  int count{0};
};

// Out-of-image padding policy.
//  ClampEdge — duplicate the border texel: exact for min/max reductions,
//              edge-weighted for averages (classic NPOT mip behavior).
//  Identity  — pad with the reduction's identity value: makes additive
//              reductions (sums) exact on any dimensions.
enum class PadMode
{
  ClampEdge,
  Identity
};

// Ceil-halving: every source texel is covered by some texel of every level
// (odd edges clamp-duplicate rather than drop), which keeps min/max pyramids
// conservative. Averages get standard SPD-style edge weighting.
//
// The same rounding is what makes PadMode::Identity *additively* exact: the
// ceil remainder carries an odd edge forward as its own texel instead of
// folding it into its neighbour, and the in-tile extent clamp (pvx/pvy in
// foldTile) then keeps the padding beyond that edge contributing the identity
// value. A sum therefore counts each source texel exactly once at any
// dimensions — do not "simplify" this to floor-halving.
inline uint2 spdHalfDims(uint2 d)
{
  return {(d.x + 1) / 2 > 1u ? (d.x + 1) / 2 : 1u,
      (d.y + 1) / 2 > 1u ? (d.y + 1) / 2 : 1u};
}

// Fills chain.dims for a halve-to-1x1 chain; returns the level count. The
// chain is capped at SPD_MAX_LEVELS, so a source wider than 2^SPD_MAX_LEVELS
// on an axis ends ABOVE 1x1 — consumers must read dims[count-1], never assume
// a single-texel top level.
template <typename T>
inline int spdBuildDims(uint2 src, MipChainView<T> &chain)
{
  int n = 0;
  uint2 d = src;
  while ((d.x > 1u || d.y > 1u) && n < SPD_MAX_LEVELS) {
    d = spdHalfDims(d);
    chain.dims[n++] = d;
  }
  chain.count = n;
  return n;
}

#ifdef __CUDACC__

namespace detail {

// Reads a previously written mip level, clamped; the LOAD for pass 2+ and
// for the in-kernel tail phase.
template <typename T>
struct LevelLoader
{
  const T *data;
  uint2 dims;

  __device__ T operator()(uint32_t x, uint32_t y) const
  {
    x = x < dims.x ? x : dims.x - 1;
    y = y < dims.y ? y : dims.y - 1;
    return data[size_t(y) * dims.x + x];
  }
};

// Folds a <=64x64 region through up to `levels` halvings using shared
// memory. `load` fetches the region's source level (clamped); results are
// written to chain.level[base..base+levels-1] at tile offset `tileOrigin`
// (in source coords). Every thread in the block participates.
template <typename T, typename OP, typename LOAD>
__device__ void foldTile(LOAD load,
    uint2 srcDims,
    uint2 tileOrigin,
    const MipChainView<T> &chain,
    int base,
    int levels,
    OP op,
    PadMode pad,
    T padValue,
    T *shmem /* 32x32 */)
{
  const uint32_t tx = threadIdx.x % 16, ty = threadIdx.x / 16;

  // Level base+0: each thread produces a 2x2 block of the 32x32 result.
  for (uint32_t sub = 0; sub < 4; sub++) {
    const uint32_t ox = tx * 2 + (sub & 1), oy = ty * 2 + (sub >> 1);
    const uint32_t sx = tileOrigin.x + ox * 2, sy = tileOrigin.y + oy * 2;
    auto padLoad = [&](uint32_t x, uint32_t y) {
      if (x < srcDims.x && y < srcDims.y)
        return load(x, y);
      if (pad == PadMode::Identity)
        return padValue;
      return load(
          x < srcDims.x ? x : srcDims.x - 1, y < srcDims.y ? y : srcDims.y - 1);
    };
    const T v = op(padLoad(sx, sy),
        padLoad(sx + 1, sy),
        padLoad(sx, sy + 1),
        padLoad(sx + 1, sy + 1));
    shmem[oy * 32 + ox] = v;
    const auto d = chain.dims[base];
    const uint32_t gx = tileOrigin.x / 2 + ox, gy = tileOrigin.y / 2 + oy;
    if (base < chain.count && gx < d.x && gy < d.y)
      chain.level[base][size_t(gy) * d.x + gx] = v;
  }
  __syncthreads();

  // Levels base+1.. : shared-memory tree, 16x16 -> ... -> 1x1 per tile.
  // Reads clamp to the source level's *valid* in-tile extent — the tile
  // padding beyond a non-pow2 image must not keep folding in, or small
  // images drown in duplicated edge texels. Clamping duplicates only the
  // true edge texel (standard NPOT mip weighting; exact for min/max).
  // Barriers stay uniform across the block: inactive threads still reach
  // every __syncthreads().
  uint32_t cur = 16; // result dimension of the next level within this tile
  for (int l = 1; l < levels; l++, cur >>= 1) {
    const bool active = tx < cur && ty < cur;
    // Valid extent of the source (in-tile) level base+l-1.
    const int li = base + l;
    const uint2 srcLvl = chain.dims[li - 1];
    const uint32_t sox = tileOrigin.x >> l, soy = tileOrigin.y >> l;
    const uint32_t pvx = srcLvl.x > sox
        ? (srcLvl.x - sox < cur * 2 ? srcLvl.x - sox : cur * 2)
        : 1u;
    const uint32_t pvy = srcLvl.y > soy
        ? (srcLvl.y - soy < cur * 2 ? srcLvl.y - soy : cur * 2)
        : 1u;
    T v{};
    if (active) {
      auto at = [&](uint32_t x, uint32_t y) {
        if (x < pvx && y < pvy)
          return shmem[y * 32 + x];
        if (pad == PadMode::Identity)
          return padValue;
        return shmem[(y < pvy ? y : pvy - 1) * 32 + (x < pvx ? x : pvx - 1)];
      };
      const uint32_t px = tx * 2, py = ty * 2;
      v = op(at(px, py), at(px + 1, py), at(px, py + 1), at(px + 1, py + 1));
    }
    __syncthreads();
    if (active) {
      shmem[ty * 32 + tx] = v;
      if (li < chain.count) {
        const auto d = chain.dims[li];
        const uint32_t gx = (tileOrigin.x >> (l + 1)) + tx;
        const uint32_t gy = (tileOrigin.y >> (l + 1)) + ty;
        if (gx < d.x && gy < d.y)
          chain.level[li][size_t(gy) * d.x + gx] = v;
      }
    }
    __syncthreads();
  }
}

template <typename T, typename OP, typename LOAD>
__global__ void spdPassKernel(LOAD load,
    uint2 srcDims,
    MipChainView<T> chain,
    int base,
    int levels,
    int tailLevels, // extra levels the last block folds (0 = no tail phase)
    OP op,
    PadMode pad,
    T padValue,
    uint32_t *tileCounter)
{
  __shared__ T shmem[32 * 32];

  const uint2 tileOrigin = {blockIdx.x * SPD_TILE, blockIdx.y * SPD_TILE};
  foldTile(
      load, srcDims, tileOrigin, chain, base, levels, op, pad, padValue, shmem);

  if (tailLevels <= 0)
    return;

  // Last-block tail: fold the just-written (<=64x64) level to the chain end.
  __shared__ bool lastBlock;
  __threadfence();
  if (threadIdx.x == 0) {
    const uint32_t done = atomicInc(tileCounter, gridDim.x * gridDim.y - 1);
    lastBlock = (done == gridDim.x * gridDim.y - 1);
  }
  __syncthreads();
  if (!lastBlock)
    return;

  const int mid = base + levels - 1;
  const LevelLoader<T> midLoader{chain.level[mid], chain.dims[mid]};
  foldTile(midLoader,
      chain.dims[mid],
      uint2{0, 0},
      chain,
      mid + 1,
      tailLevels,
      op,
      pad,
      padValue,
      shmem);
}

} // namespace detail

// Reduces `load` (srcDims) into the mip chain (level[0] = half resolution of
// the source, halving down to chain.count levels — 1x1 when
// chain.count == spdLevelCount(srcDims)). `tileCounter` is one device
// uint32_t, zeroed by the caller. All work runs on `stream`.
template <typename T, typename OP, typename LOAD>
inline void singlePassDownsample(cudaStream_t stream,
    LOAD load,
    uint2 srcDims,
    const MipChainView<T> &chain,
    OP op,
    uint32_t *tileCounter,
    PadMode pad = PadMode::ClampEdge,
    T padValue = T{})
{
  auto tilesFor = [](uint2 d) {
    return dim3(
        (d.x + SPD_TILE - 1) / SPD_TILE, (d.y + SPD_TILE - 1) / SPD_TILE);
  };

  int base = 0;
  uint2 dims = srcDims;
  bool first = true;
  while (base < chain.count) {
    const int levels = chain.count - base < SPD_LEVELS_PER_PASS
        ? chain.count - base
        : SPD_LEVELS_PER_PASS;
    const dim3 grid = tilesFor(dims);
    const int remaining = chain.count - (base + levels);
    // Fold the tail in-kernel when one block can cover the mid level.
    const int mid = base + levels - 1;
    const bool tailFits = remaining > 0 && chain.dims[mid].x <= SPD_TILE
        && chain.dims[mid].y <= SPD_TILE;
    const int tail = tailFits
        ? (remaining < SPD_LEVELS_PER_PASS ? remaining : SPD_LEVELS_PER_PASS)
        : 0;

    if (first) {
      detail::spdPassKernel<<<grid, 256, 0, stream>>>(load,
          dims,
          chain,
          base,
          levels,
          tail,
          op,
          pad,
          padValue,
          tileCounter);
      first = false;
    } else {
      const detail::LevelLoader<T> loader{
          chain.level[base - 1], chain.dims[base - 1]};
      detail::spdPassKernel<<<grid, 256, 0, stream>>>(loader,
          dims,
          chain,
          base,
          levels,
          tail,
          op,
          pad,
          padValue,
          tileCounter);
    }
    base += levels + tail;
    if (base < chain.count)
      dims = chain.dims[base - 1];
  }
}

#endif // __CUDACC__

} // namespace spd
