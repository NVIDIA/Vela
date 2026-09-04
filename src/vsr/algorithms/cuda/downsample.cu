// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_algorithms
#include "vsr/algorithms/cuda/downsample.hpp"
#include "../math/color.h"
#include "detail/SinglePassDownsampler.h"
// std
#include <cstdio>
#include <vector>

namespace vsr::algorithms::cuda {

namespace {

// Reports a failed CUDA call once, at the point it happened, and returns
// false so the caller can fold it into an empty optional. Checking every
// status is a documented requirement (STYLEGUIDE Sec. 14): a silently failed
// allocation or launch otherwise resurfaces as an unrelated error in whatever
// code runs next.
bool checkCuda(cudaError_t status, const char *what)
{
  if (status == cudaSuccess)
    return true;
  fprintf(stderr,
      "[vsr] meanLogLuminance: %s failed: %s\n",
      what,
      cudaGetErrorString(status));
  return false;
}

struct LogLuminanceLoader
{
  const float *hdr;
  uint32_t width;

  __device__ float operator()(uint32_t x, uint32_t y) const
  {
    const size_t idx = (size_t(y) * width + x) * 4;
    return log2f(math::clampedLuminance(
        hdr[idx + 0], hdr[idx + 1], hdr[idx + 2]));
  }
};

struct Sum4
{
  __device__ float operator()(float a, float b, float c, float d) const
  {
    return a + b + c + d;
  }
};

} // namespace

MeanLogLuminanceScratch::~MeanLogLuminanceScratch()
{
  if (storage)
    cudaFree(storage);
}

std::optional<float> meanLogLuminance(MeanLogLuminanceScratch &scratch,
    cudaStream_t stream,
    const float *hdrColor,
    uint32_t width,
    uint32_t height)
{
  if (width == 0 || height == 0 || !hdrColor)
    return {};

  spd::MipChainView<float> chain;
  spd::spdBuildDims(uint2{width, height}, chain);
  if (chain.count == 0) { // 1x1 source: nothing to reduce
    float4 texel;
    if (!checkCuda(cudaMemcpyAsync(&texel,
                       hdrColor,
                       sizeof(texel),
                       cudaMemcpyDeviceToHost,
                       stream),
            "single-texel copy"))
      return {};
    if (!checkCuda(cudaStreamSynchronize(stream), "single-texel sync"))
      return {};
    return log2f(math::clampedLuminance(texel.x, texel.y, texel.z));
  }

  // One allocation backs every level plus the tile counter.
  size_t texels = 0;
  for (int i = 0; i < chain.count; i++)
    texels += size_t(chain.dims[i].x) * chain.dims[i].y;
  const size_t capacity = texels + 1; // + 1 float for the tile counter

  // Grow-only scratch: reallocate only when this image needs more room than
  // any seen so far. cudaMalloc/cudaFree (not their *Async forms) on
  // purpose — the resize is rare, and staying out of the stream-ordered
  // allocator keeps per-frame scratch off the mempool release/re-map path
  // entirely.
  //
  // The new block is allocated *before* the old one is freed, so a failure
  // here leaves the existing scratch usable and the next frame can retry at
  // the old capacity instead of being stuck with nothing.
  if (scratch.capacityTexels < capacity) {
    float *grown = nullptr;
    if (!checkCuda(cudaMalloc((void **)&grown, capacity * sizeof(float)),
            "scratch allocation"))
      return {};
    if (scratch.storage)
      cudaFree(scratch.storage);
    scratch.storage = grown;
    scratch.capacityTexels = capacity;
  }

  float *cursor = scratch.storage;
  for (int i = 0; i < chain.count; i++) {
    chain.level[i] = cursor;
    cursor += size_t(chain.dims[i].x) * chain.dims[i].y;
  }
  auto *counter = reinterpret_cast<uint32_t *>(scratch.storage + texels);
  if (!checkCuda(cudaMemsetAsync(counter, 0, sizeof(uint32_t), stream),
          "tile counter reset"))
    return {};

  // Identity (zero) padding + a sum reduction counts every texel exactly
  // once regardless of dimensions; the division below yields the exact mean.
  spd::singlePassDownsample(stream,
      LogLuminanceLoader{hdrColor, width},
      uint2{width, height},
      chain,
      Sum4{},
      counter,
      spd::PadMode::Identity,
      0.f);
  // Catches launch-configuration errors only. A fault *inside* a kernel is
  // asynchronous and surfaces at the next synchronizing call — here the
  // stream sync below, which is checked for exactly that reason.
  if (!checkCuda(cudaGetLastError(), "downsample launch"))
    return {};

  // 16 chain levels fold at most 65536 per axis; a larger source leaves a
  // >1x1 top level whose partial sums still cover every texel exactly once —
  // finish the reduction on the host.
  const uint2 topDims = chain.dims[chain.count - 1];
  std::vector<float> top(size_t(topDims.x) * topDims.y);
  if (!checkCuda(cudaMemcpyAsync(top.data(),
                     chain.level[chain.count - 1],
                     top.size() * sizeof(float),
                     cudaMemcpyDeviceToHost,
                     stream),
          "top-level readback"))
    return {};
  if (!checkCuda(cudaStreamSynchronize(stream), "reduction sync"))
    return {};

  float sum = 0.f;
  for (float v : top)
    sum += v;
  return sum / (float(width) * float(height));
}

} // namespace vsr::algorithms::cuda
