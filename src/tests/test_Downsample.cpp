// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"

#ifdef VSR_ALGORITHMS_HAS_CUDA

// vsr
#include "vsr/algorithms/cpu/downsample.hpp"
#include "vsr/algorithms/cuda/downsample.hpp"
// cuda
#include <cuda_runtime_api.h>
// std
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

bool cudaAvailable()
{
  int n = 0;
  return cudaGetDeviceCount(&n) == cudaSuccess && n > 0;
}

// Expands per-pixel luminance values into an interleaved RGBA32F image
// (grayscale, so luminance(r,g,b) == the value) with opaque alpha.
std::vector<float> toRGBA(const std::vector<float> &lum)
{
  std::vector<float> rgba(lum.size() * 4);
  for (size_t i = 0; i < lum.size(); i++) {
    rgba[i * 4 + 0] = lum[i];
    rgba[i * 4 + 1] = lum[i];
    rgba[i * 4 + 2] = lum[i];
    rgba[i * 4 + 3] = 1.f;
  }
  return rgba;
}

// Owns a device copy of an RGBA32F image for the duration of a test.
struct DeviceImage
{
  float *data{nullptr};

  explicit DeviceImage(const std::vector<float> &rgba)
  {
    REQUIRE(cudaMalloc((void **)&data, rgba.size() * sizeof(float))
        == cudaSuccess);
    REQUIRE(cudaMemcpy(data,
                rgba.data(),
                rgba.size() * sizeof(float),
                cudaMemcpyHostToDevice)
        == cudaSuccess);
  }
  ~DeviceImage()
  {
    if (data)
      cudaFree(data);
  }
  DeviceImage(const DeviceImage &) = delete;
  DeviceImage &operator=(const DeviceImage &) = delete;
};

// Uploads an image of the given per-pixel luminances and returns the SPD mean,
// reducing through caller-held persistent scratch.
std::optional<float> gpuMeanLogLum(
    vsr::algorithms::cuda::MeanLogLuminanceScratch &scratch,
    const std::vector<float> &lum,
    uint32_t w,
    uint32_t h)
{
  const DeviceImage image(toRGBA(lum));
  return vsr::algorithms::cuda::meanLogLuminance(
      scratch, cudaStream_t{0}, image.data, w, h);
}

// gpuMeanLogLum on fresh scratch.
std::optional<float> gpuMeanLogLum(
    const std::vector<float> &lum, uint32_t w, uint32_t h)
{
  vsr::algorithms::cuda::MeanLogLuminanceScratch scratch;
  return gpuMeanLogLum(scratch, lum, w, h);
}

// Unwraps a reduction that is expected to succeed.
float require(const std::optional<float> &mean)
{
  REQUIRE(mean.has_value());
  return *mean;
}

} // namespace

SCENARIO("vsr::algorithms::cuda::meanLogLuminance", "[Downsample]")
{
  if (!cudaAvailable()) {
    SUCCEED("no CUDA device — skipping");
    return;
  }

  GIVEN("a constant-luminance power-of-two image")
  {
    const uint32_t w = 64, h = 64;
    std::vector<float> lum(size_t(w) * h, 0.5f);
    THEN("the mean log-luminance is exactly log2 of that constant")
    {
      REQUIRE(require(gpuMeanLogLum(lum, w, h)) == Approx(-1.f).margin(1e-4));
    }
  }

  GIVEN("a half-bright / half-dark power-of-two image")
  {
    const uint32_t w = 128, h = 64;
    std::vector<float> lum(size_t(w) * h);
    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++)
        lum[size_t(y) * w + x] = x < w / 2 ? 0.25f : 4.f;
    }
    THEN("the mean log-luminance is the average of both halves")
    {
      // log2(0.25) = -2, log2(4) = 2 -> mean 0
      REQUIRE(require(gpuMeanLogLum(lum, w, h)) == Approx(0.f).margin(1e-3));
    }
  }

  GIVEN("a wide image needing multiple downsample passes")
  {
    const uint32_t w = 5000, h = 117; // 13 levels: exercises the host loop
    std::vector<float> lum(size_t(w) * h, 2.f);
    THEN("a constant image still reduces to its exact log-luminance")
    {
      REQUIRE(require(gpuMeanLogLum(lum, w, h)) == Approx(1.f).margin(1e-3));
    }
  }

  GIVEN("a large multi-pass image with a different value in every texel")
  {
    // The constant-valued multi-pass case above cannot see a tail fold that
    // double-counts or drops a texel: any subset of a constant field still
    // averages to that constant. Varying every texel makes the multi-pass
    // tail path (tailFits / remaining levels) observable, checked against a
    // double-precision host reference.
    const uint32_t w = 1531, h = 373; // non-pow2 on both axes, multi-pass
    std::vector<float> lum(size_t(w) * h);
    double exact = 0.0;
    for (size_t i = 0; i < lum.size(); i++) {
      // Deterministic, non-repeating, spanning a wide dynamic range.
      const uint32_t bits = uint32_t(i) * 2654435761u;
      lum[i] = 0.01f + 8.f * float(bits >> 8) / float(1u << 24);
      exact += std::log2(lum[i]);
    }
    exact /= double(lum.size());

    THEN("the SPD reduction matches the host reference")
    {
      REQUIRE(require(gpuMeanLogLum(lum, w, h))
          == Approx(exact).margin(1e-3));
    }
  }

  GIVEN("a non-power-of-two gradient image")
  {
    const uint32_t w = 37, h = 23;
    std::vector<float> lum(size_t(w) * h);
    float mn = 1e30f, mx = -1e30f;
    double exact = 0.0;
    for (size_t i = 0; i < lum.size(); i++) {
      lum[i] = 0.1f + 2.f * float(i) / float(lum.size());
      const float l = std::log2(lum[i]);
      exact += l;
      mn = std::min(mn, l);
      mx = std::max(mx, l);
    }
    exact /= double(lum.size());
    THEN("the SPD mean matches the exact mean (identity padding)")
    {
      const float mean = require(gpuMeanLogLum(lum, w, h));
      REQUIRE(mean >= mn);
      REQUIRE(mean <= mx);
      REQUIRE(mean == Approx(exact).margin(1e-3));
    }
  }

  GIVEN("a persistent scratch reused across images")
  {
    vsr::algorithms::cuda::MeanLogLuminanceScratch scratch;

    // Start big so later calls exercise the reuse (no-realloc) path.
    const uint32_t big = 128, small = 37, tall = 23;
    std::vector<float> lumBig(size_t(big) * big, 0.25f);
    std::vector<float> lumSmall(size_t(small) * tall);
    double exact = 0.0;
    for (size_t i = 0; i < lumSmall.size(); i++) {
      lumSmall[i] = 0.5f + 4.f * float(i) / float(lumSmall.size());
      exact += std::log2(lumSmall[i]);
    }
    exact /= double(lumSmall.size());

    THEN("every reuse stays exact, growing the scratch at most once")
    {
      REQUIRE(require(gpuMeanLogLum(scratch, lumBig, big, big))
          == Approx(-2.f).margin(1e-3));
      const size_t grown = scratch.capacityTexels;
      REQUIRE(grown > 0);

      // Smaller image: reuses the buffer without reallocating.
      const float mean = require(gpuMeanLogLum(scratch, lumSmall, small, tall));
      REQUIRE(scratch.capacityTexels == grown);
      REQUIRE(mean == Approx(exact).margin(1e-3));

      // Same image again: still exact, still no realloc.
      REQUIRE(require(gpuMeanLogLum(scratch, lumSmall, small, tall))
          == Approx(exact).margin(1e-3));
      REQUIRE(scratch.capacityTexels == grown);

      // Oscillating back up past the high-water mark grows exactly once more
      // and stays correct; dropping back down must not shrink or realloc.
      std::vector<float> lumBigger(size_t(256) * 256, 8.f);
      REQUIRE(require(gpuMeanLogLum(scratch, lumBigger, 256u, 256u))
          == Approx(3.f).margin(1e-3));
      const size_t regrown = scratch.capacityTexels;
      REQUIRE(regrown > grown);

      REQUIRE(require(gpuMeanLogLum(scratch, lumSmall, small, tall))
          == Approx(exact).margin(1e-3));
      REQUIRE(scratch.capacityTexels == regrown);

      REQUIRE(require(gpuMeanLogLum(scratch, lumBigger, 256u, 256u))
          == Approx(3.f).margin(1e-3));
      REQUIRE(scratch.capacityTexels == regrown);
    }
  }

  GIVEN("a single-texel image")
  {
    // chain.count == 0: this takes the dedicated copy-one-texel path rather
    // than launching the downsampler at all.
    THEN("the mean is that texel's log-luminance")
    {
      REQUIRE(require(gpuMeanLogLum({8.f}, 1u, 1u)) == Approx(3.f).margin(1e-4));
    }

    THEN("a black texel is held up by the luminance floor")
    {
      REQUIRE(require(gpuMeanLogLum({0.f}, 1u, 1u))
          == Approx(std::log2(1e-4f)).margin(1e-3));
    }
  }

  GIVEN("images whose dimensions straddle the 64-texel tile boundary")
  {
    // One texel over a tile forces a second, almost entirely padded tile —
    // the case where identity padding has to contribute exactly nothing.
    for (const uint32_t n : {63u, 64u, 65u, 129u}) {
      std::vector<float> lum(size_t(n) * n, 8.f);
      THEN("a constant image reduces exactly regardless of tiling")
      {
        INFO("dimension " << n);
        REQUIRE(require(gpuMeanLogLum(lum, n, n)) == Approx(3.f).margin(1e-3));
      }
    }
  }

  GIVEN("single-row and single-column images")
  {
    // Degenerate aspect ratios halve to 1 on one axis long before the other,
    // exercising the dimension clamp in spdHalfDims.
    std::vector<float> row(200, 4.f);
    THEN("a 200x1 row reduces exactly")
    {
      REQUIRE(require(gpuMeanLogLum(row, 200u, 1u)) == Approx(2.f).margin(1e-3));
    }
    THEN("a 1x200 column reduces exactly")
    {
      REQUIRE(require(gpuMeanLogLum(row, 1u, 200u)) == Approx(2.f).margin(1e-3));
    }
  }

  GIVEN("an image containing NaN and infinite texels")
  {
    // The clamp has to be applied on the device exactly as on the host, or a
    // single firefly propagates through the whole additive reduction.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> lum(size_t(16) * 16, 1.f);
    lum[0] = nan;
    lum[1] = inf;

    THEN("the result stays finite")
    {
      const float mean = require(gpuMeanLogLum(lum, 16u, 16u));
      REQUIRE(std::isfinite(mean));
      // 254 texels at log2(1) = 0, plus the floor and the ceiling.
      const float expected = (std::log2(1e-4f) + std::log2(1e8f)) / 256.f;
      REQUIRE(mean == Approx(expected).margin(1e-3));
    }
  }

  GIVEN("a degenerate request")
  {
    const DeviceImage image(toRGBA({1.f, 1.f, 1.f, 1.f}));

    THEN("no value is returned, rather than a fabricated mean of zero")
    {
      namespace cuda = vsr::algorithms::cuda;
      cuda::MeanLogLuminanceScratch scratch;
      REQUIRE_FALSE(
          cuda::meanLogLuminance(scratch, cudaStream_t{0}, image.data, 0u, 2u)
              .has_value());
      REQUIRE_FALSE(
          cuda::meanLogLuminance(scratch, cudaStream_t{0}, image.data, 2u, 0u)
              .has_value());
      REQUIRE_FALSE(
          cuda::meanLogLuminance(scratch, cudaStream_t{0}, nullptr, 2u, 2u)
              .has_value());
    }
  }

  GIVEN("the same image reduced on both backends")
  {
    // The parity claim in the README ("same API and exact result on both
    // backends") is what this pins: a non-power-of-two image with wide
    // dynamic range plus the two clamped extremes, compared host to device.
    const uint32_t w = 101, h = 57;
    std::vector<float> lum(size_t(w) * h);
    for (size_t i = 0; i < lum.size(); i++)
      lum[i] = 0.01f + 100.f * float(i) / float(lum.size());
    lum[7] = 0.f; // floor
    lum[42] = std::numeric_limits<float>::infinity(); // ceiling
    lum[99] = std::numeric_limits<float>::quiet_NaN(); // floor

    THEN("the CPU and CUDA reductions agree")
    {
      const std::vector<float> rgba = toRGBA(lum);
      const float host =
          require(vsr::algorithms::cpu::meanLogLuminance(rgba.data(), w, h));
      const float device = require(gpuMeanLogLum(lum, w, h));
      REQUIRE(std::isfinite(host));
      REQUIRE(device == Approx(host).margin(1e-3));
    }
  }
}

#else

SCENARIO("vsr::algorithms::cuda::meanLogLuminance", "[Downsample]")
{
  SUCCEED("built without CUDA — skipping");
}

#endif
