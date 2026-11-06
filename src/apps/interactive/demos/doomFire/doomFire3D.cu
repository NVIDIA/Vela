// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "doomFire3D.h"
// thrust
#include <thrust/device_vector.h>
#include <thrust/execution_policy.h>
#include <thrust/fill.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>
// cuda
#include <cuda_runtime.h>
// std
#include <cmath>

namespace vsr::demo {

namespace {

__host__ __device__ inline uint32_t pcgHash(uint32_t v)
{
  const uint32_t state = v * 747796405u + 2891336453u;
  const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

struct FireStep
{
  const uint8_t *src;
  uint8_t *dst;
  int nx, ny, nz;
  uint32_t seed;
  uint8_t baseIntensity;
  int maxDecay;
  float windX, windZ;
  bool jitter;

  __host__ __device__ void operator()(size_t idx) const
  {
    const int x = int(idx % nx);
    const int y = int((idx / nx) % ny);
    const int z = int(idx / (size_t(nx) * ny));

    if (y == 0) {
      dst[idx] = baseIntensity;
      return;
    }

    uint32_t h = pcgHash(uint32_t(idx) ^ pcgHash(seed));

    // Like the original, the decay amount also drives how far the wind
    // carries the flame: cells that cool more are blown further.
    const int decay = int(h % uint32_t(maxDecay + 1));
    h = pcgHash(h);
    const float windScale = maxDecay > 0 ? float(decay) / maxDecay : 0.f;

    int sx = x - int(lroundf(windX * windScale));
    int sz = z - int(lroundf(windZ * windScale));
    if (jitter) {
      sx += int(h % 3u) - 1;
      sz += int((h / 3u) % 3u) - 1;
    }

    if (sx < 0 || sx >= nx || sz < 0 || sz >= nz) {
      dst[idx] = 0;
      return;
    }

    const int below = src[sx + size_t(nx) * ((y - 1) + size_t(ny) * sz)];
    dst[idx] = uint8_t(below > decay ? below - decay : 0);
  }
};

} // namespace

struct DoomFire3D::Impl
{
  int nx{0}, ny{0}, nz{0};
  uint32_t frame{0};
  thrust::device_vector<uint8_t> a, b; // 'a' always holds the latest state
};

DoomFire3D::DoomFire3D() : m_impl(std::make_unique<Impl>()) {}

DoomFire3D::~DoomFire3D() = default;

void DoomFire3D::resize(int nx, int ny, int nz, uint8_t baseIntensity)
{
  auto &s = *m_impl;
  s.nx = nx;
  s.ny = ny;
  s.nz = nz;
  const size_t n = size_t(nx) * ny * nz;
  s.a.assign(n, 0);
  s.b.assign(n, 0);
  reset(baseIntensity);
}

void DoomFire3D::reset(uint8_t baseIntensity)
{
  auto &s = *m_impl;
  thrust::fill(s.a.begin(), s.a.end(), uint8_t(0));
  thrust::fill(s.b.begin(), s.b.end(), uint8_t(0));
  // Light the base layer (y == 0) of every z slice.
  for (int z = 0; z < s.nz; z++) {
    auto first = s.a.begin() + size_t(s.nx) * s.ny * z;
    thrust::fill(first, first + s.nx, baseIntensity);
  }
  s.frame = 0;
}

void DoomFire3D::step(const Params &p, int steps)
{
  auto &s = *m_impl;
  const size_t n = s.a.size();
  if (n == 0)
    return;

  for (int i = 0; i < steps; i++) {
    FireStep op{thrust::raw_pointer_cast(s.a.data()),
        thrust::raw_pointer_cast(s.b.data()),
        s.nx,
        s.ny,
        s.nz,
        s.frame++,
        p.baseIntensity,
        p.maxDecay < 0 ? 0 : p.maxDecay,
        p.windX,
        p.windZ,
        p.jitter};
    thrust::for_each(thrust::device,
        thrust::make_counting_iterator(size_t(0)),
        thrust::make_counting_iterator(n),
        op);
    s.a.swap(s.b);
  }
}

void DoomFire3D::copyTo(void *dst) const
{
  auto &s = *m_impl;
  cudaMemcpy(
      dst, thrust::raw_pointer_cast(s.a.data()), s.a.size(), cudaMemcpyDefault);
}

} // namespace vsr::demo
