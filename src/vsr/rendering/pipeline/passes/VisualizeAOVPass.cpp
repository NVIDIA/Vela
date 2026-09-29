// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "VisualizeAOVPass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/visualizeAOV.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/visualizeAOV.hpp"
#endif

namespace vsr::rendering {

// VisualizeAOVPass definitions ///////////////////////////////////////////////

VisualizeAOVPass::VisualizeAOVPass() = default;

VisualizeAOVPass::~VisualizeAOVPass() = default;

void VisualizeAOVPass::setAOVType(AOVType type)
{
  m_aovType = type;
  setEnabled(type != AOVType::NONE);
}

void VisualizeAOVPass::setDepthRange(float minDepth, float maxDepth)
{
  m_minDepth = minDepth;
  m_maxDepth = maxDepth;
}

void VisualizeAOVPass::setEdgeInvert(bool invert)
{
  m_edgeInvert = invert;
}

ImageChannels requiredChannels(AOVType type)
{
  switch (type) {
  case AOVType::DEPTH:
    return ImageChannels::DEPTH;
  case AOVType::ALBEDO:
    return ImageChannels::ALBEDO;
  case AOVType::NORMAL:
    return ImageChannels::NORMAL;
  case AOVType::EDGES:
  case AOVType::OBJECT_ID:
    return ImageChannels::OBJECT_ID;
  case AOVType::PRIMITIVE_ID:
    return ImageChannels::PRIMITIVE_ID;
  case AOVType::INSTANCE_ID:
    return ImageChannels::INSTANCE_ID;
  default:
    return ImageChannels::NONE;
  }
}

ImageChannels VisualizeAOVPass::requiredChannels() const
{
  return rendering::requiredChannels(m_aovType);
}

void VisualizeAOVPass::render(ImageBuffers &b, FrameState & /*frame*/)
{
  if (m_aovType == AOVType::NONE)
    return;

  const auto size = dimensions();

#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    namespace alg = vsr::algorithms::cuda;
    switch (m_aovType) {
    case AOVType::DEPTH:
      alg::visualizeDepth(
          b.stream, b.depth, b.color, m_minDepth, m_maxDepth, size.x, size.y);
      break;
    case AOVType::ALBEDO:
      alg::visualizeAlbedo(b.stream, b.albedo, b.color, size.x, size.y);
      break;
    case AOVType::NORMAL:
      alg::visualizeNormal(b.stream, b.normal, b.color, size.x, size.y);
      break;
    case AOVType::EDGES:
      alg::visualizeEdges(
          b.stream, b.objectId, b.color, m_edgeInvert, size.x, size.y);
      break;
    case AOVType::OBJECT_ID:
      alg::visualizeId(b.stream, b.objectId, b.color, size.x, size.y);
      break;
    case AOVType::PRIMITIVE_ID:
      alg::visualizeId(b.stream, b.primitiveId, b.color, size.x, size.y);
      break;
    case AOVType::INSTANCE_ID:
      alg::visualizeId(b.stream, b.instanceId, b.color, size.x, size.y);
      break;
    default:
      break;
    }
    return;
  }
#endif

  namespace alg = vsr::algorithms::cpu;
  switch (m_aovType) {
  case AOVType::DEPTH:
    alg::visualizeDepth(
        b.depth, b.color, m_minDepth, m_maxDepth, size.x, size.y);
    break;
  case AOVType::ALBEDO:
    alg::visualizeAlbedo(b.albedo, b.color, size.x, size.y);
    break;
  case AOVType::NORMAL:
    alg::visualizeNormal(b.normal, b.color, size.x, size.y);
    break;
  case AOVType::EDGES:
    alg::visualizeEdges(b.objectId, b.color, m_edgeInvert, size.x, size.y);
    break;
  case AOVType::OBJECT_ID:
    alg::visualizeId(b.objectId, b.color, size.x, size.y);
    break;
  case AOVType::PRIMITIVE_ID:
    alg::visualizeId(b.primitiveId, b.color, size.x, size.y);
    break;
  case AOVType::INSTANCE_ID:
    alg::visualizeId(b.instanceId, b.color, size.x, size.y);
    break;
  default:
    break;
  }
}

} // namespace vsr::rendering
