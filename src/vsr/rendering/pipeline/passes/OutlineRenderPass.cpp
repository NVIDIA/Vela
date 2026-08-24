// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OutlineRenderPass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/outline.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/outline.hpp"
#endif

namespace vsr::rendering {

// OutlineRenderPass definitions //////////////////////////////////////////////

OutlineRenderPass::OutlineRenderPass() = default;

OutlineRenderPass::~OutlineRenderPass() = default;

void OutlineRenderPass::setOutlineId(uint32_t id)
{
  m_outlineId = id;
}

void OutlineRenderPass::render(ImageBuffers &b, int stageId)
{
  if (!b.objectId || stageId == 0 || m_outlineId == ~0u)
    return;

  const auto size = getDimensions();

#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    vsr::algorithms::cuda::outlineObject(
        b.stream, b.objectId, b.color, m_outlineId, size.x, size.y);
    return;
  }
#endif
  vsr::algorithms::cpu::outlineObject(
      b.objectId, b.color, m_outlineId, size.x, size.y);
}

} // namespace vsr::rendering
