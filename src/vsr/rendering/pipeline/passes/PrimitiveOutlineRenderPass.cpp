// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PrimitiveOutlineRenderPass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/outline.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/outline.hpp"
#endif
// helium
#include <helium/helium_math.h>

namespace vsr::rendering {

PrimitiveOutlineRenderPass::PrimitiveOutlineRenderPass() = default;

PrimitiveOutlineRenderPass::~PrimitiveOutlineRenderPass() = default;

void PrimitiveOutlineRenderPass::setOutlineColor(
    const vsr::math::float4 &color)
{
  m_outlineColor = color;
}

void PrimitiveOutlineRenderPass::setThickness(uint32_t thickness)
{
  m_thickness = thickness;
}

void PrimitiveOutlineRenderPass::render(ImageBuffers &b, int stageId)
{
  if (!b.objectId || !b.primitiveId || stageId == 0)
    return;

  const auto size = getDimensions();
  const auto outlineColor = helium::cvt_color_to_uint32(m_outlineColor);

#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    vsr::algorithms::cuda::outlinePrimitives(b.stream,
        b.objectId,
        b.primitiveId,
        b.color,
        outlineColor,
        m_thickness,
        size.x,
        size.y);
    return;
  }
#endif
  vsr::algorithms::cpu::outlinePrimitives(b.objectId,
      b.primitiveId,
      b.color,
      outlineColor,
      m_thickness,
      size.x,
      size.y);
}

} // namespace vsr::rendering
