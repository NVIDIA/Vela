// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AutoExposurePass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/autoExposure.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/autoExposure.hpp"
#endif
// std
#include <algorithm>
#include <cmath>

namespace vsr::rendering {

namespace {

constexpr uint32_t SAMPLE_COUNT = 16384;
constexpr float MIN_EXPOSURE = -20.f;
constexpr float MAX_EXPOSURE = 20.f;
constexpr float MID_GRAY = 0.18f;

} // namespace

AutoExposurePass::AutoExposurePass() = default;

AutoExposurePass::~AutoExposurePass() = default;

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

void AutoExposurePass::render(ImageBuffers &b, int stageId)
{
  if (stageId == 0 || !m_hdrEnabled)
    return;

  const auto size = getDimensions();
  const uint32_t totalPixels = size.x * size.y;
  if (totalPixels == 0 || !b.hdrColor)
    return;

  const uint32_t stride = std::max(1u, totalPixels / SAMPLE_COUNT);
  const uint32_t numSamples = (totalPixels + stride - 1) / stride;
  if (numSamples == 0)
    return;

  float sumLogLum = 0.f;
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    sumLogLum = vsr::algorithms::cuda::sumLogLuminance(
        b.stream, b.hdrColor, numSamples, stride);
  } else
#endif
  {
    sumLogLum =
        vsr::algorithms::cpu::sumLogLuminance(b.hdrColor, numSamples, stride);
  }

  const float avgLum = std::exp2(sumLogLum / float(numSamples));
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
