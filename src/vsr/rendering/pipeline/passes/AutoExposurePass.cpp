// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AutoExposurePass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/downsample.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/downsample.hpp"
#endif
// std
#include <algorithm>
#include <cmath>
#include <optional>

namespace vsr::rendering {

namespace {

constexpr float MIN_EXPOSURE = -20.f;
constexpr float MAX_EXPOSURE = 20.f;
constexpr float MID_GRAY = 0.18f;

} // namespace

AutoExposurePass::AutoExposurePass() = default;

AutoExposurePass::~AutoExposurePass() = default;

// Device scratch for the CUDA mean-log-luminance reduction, held by the pass
// so per-frame cost never includes a device alloc/free cycle. Opaque in the
// header because AutoExposurePass.h must compile without CUDA headers.
struct AutoExposurePass::Scratch
{
#ifdef VSR_ALGORITHMS_HAS_CUDA
  vsr::algorithms::cuda::MeanLogLuminanceScratch meanLogLuminance;
#endif
};

void AutoExposurePass::setHDREnabled(bool enabled)
{
  if (enabled && !m_hdrEnabled)
    m_hasExposure = false;
  m_hdrEnabled = enabled;
}

float AutoExposurePass::currentExposure() const
{
  return m_currentExposure;
}

void AutoExposurePass::render(ImageBuffers &b)
{
  if (!m_hdrEnabled)
    return;

  const auto size = dimensions();
  const uint32_t totalPixels = size.x * size.y;
  if (totalPixels == 0 || !b.hdrColor)
    return;

  // Mean log-luminance: exact on CUDA (SPD downsampler), a per-frame
  // re-jittered stratified estimate on the host.
  std::optional<float> meanLogLum;
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    if (!m_scratch)
      m_scratch = std::make_unique<Scratch>();
    meanLogLum = vsr::algorithms::cuda::meanLogLuminance(
        m_scratch->meanLogLuminance, b.stream, b.hdrColor, size.x, size.y);
  } else
#endif
  {
    meanLogLum = vsr::algorithms::cpu::meanLogLuminance(
        b.hdrColor, size.x, size.y, m_frameIndex++);
  }

  // A failed reduction carries no exposure information, so hold the last
  // good value rather than adapting toward a fabricated one — treating the
  // absent result as 0.f would drive the loop toward luminance 1.0 and show
  // up as a visible brightness lurch on a transient CUDA failure.
  if (!meanLogLum) {
    b.exposure = m_currentExposure;
    return;
  }

  const float avgLum = std::exp2(*meanLogLum);
  const float targetExposure =
      std::clamp(std::log2(MID_GRAY / avgLum), MIN_EXPOSURE, MAX_EXPOSURE);

  if (!m_hasExposure) {
    m_currentExposure = targetExposure;
    m_hasExposure = true;
  } else {
    m_currentExposure += (targetExposure - m_currentExposure) * m_response;
  }

  b.exposure = m_currentExposure;
}

} // namespace vsr::rendering
