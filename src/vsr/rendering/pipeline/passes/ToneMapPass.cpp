// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ToneMapPass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/toneMap.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/toneMap.hpp"
#endif
// std
#include <cmath>

namespace vsr::rendering {

// ToneMapPass definitions /////////////////////////////////////////////////////

ToneMapPass::ToneMapPass() = default;

ToneMapPass::~ToneMapPass() = default;

void ToneMapPass::setOperator(ToneMapOperator op)
{
  m_operator = op;
}

void ToneMapPass::setAutoExposureEnabled(bool enabled)
{
  m_autoExposureEnabled = enabled;
}

void ToneMapPass::setExposure(float exposure)
{
  m_exposure = exposure;
}

void ToneMapPass::setHDREnabled(bool enabled)
{
  m_hdrEnabled = enabled;
}

ImageChannels ToneMapPass::requiredChannels() const
{
  return m_hdrEnabled ? ImageChannels::HDR_COLOR : ImageChannels::NONE;
}

void ToneMapPass::render(ImageBuffers &b, FrameState &frame)
{
  if (!m_hdrEnabled)
    return;

  const auto size = dimensions();
  const uint32_t totalPixels = size.x * size.y;
  if (totalPixels == 0 || !b.hdrColor)
    return;

  const float exposure =
      (m_autoExposureEnabled ? frame.exposure : 0.f) + m_exposure;
  const float exposureScale = std::exp2(exposure);

#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    vsr::algorithms::cuda::toneMap(
        b.stream, b.hdrColor, totalPixels, exposureScale, m_operator);
    return;
  }
#endif
  vsr::algorithms::cpu::toneMap(
      b.hdrColor, totalPixels, exposureScale, m_operator);
}

} // namespace vsr::rendering
