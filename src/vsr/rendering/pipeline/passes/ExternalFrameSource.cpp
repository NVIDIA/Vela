// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExternalFrameSource.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/clearBuffers.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/clearBuffers.hpp"
#endif
// helium
#include <helium/helium_math.h>

namespace vsr::rendering {

ExternalFrameSource::ExternalFrameSource() = default;

ExternalFrameSource::~ExternalFrameSource() = default;

void ExternalFrameSource::setFrame(const std::vector<uint8_t> *frame)
{
  m_frame = frame;
}

void ExternalFrameSource::setFallbackColor(const vsr::math::float4 &color)
{
  m_fallbackColor = color;
}

void ExternalFrameSource::render(ImageBuffers &b)
{
  const auto size = dimensions();
  const size_t totalPixels = size_t(size.x) * size_t(size.y);

  if (m_frame && m_frame->size() == totalPixels * 4) {
    detail::memcpy_(b.color, m_frame->data(), totalPixels * 4);
    return;
  }

  const uint32_t c = helium::cvt_color_to_uint32(m_fallbackColor);
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    vsr::algorithms::cuda::fill(b.stream, b.color, uint32_t(totalPixels), c);
    return;
  }
#endif
  vsr::algorithms::cpu::fill(b.color, uint32_t(totalPixels), c);
}

} // namespace vsr::rendering
