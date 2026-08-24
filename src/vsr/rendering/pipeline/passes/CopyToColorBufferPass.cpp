// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CopyToColorBufferPass.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::rendering {

CopyToColorBufferPass::CopyToColorBufferPass() = default;

CopyToColorBufferPass::~CopyToColorBufferPass() = default;

void CopyToColorBufferPass::setExternalBuffer(std::vector<uint8_t> &buffer)
{
  m_externalBuffer = &buffer;
}

void CopyToColorBufferPass::render(ImageBuffers &b, int /*stageId*/)
{
  if (!b.color) {
    vsr::core::logError("[CopyToColorBufferPass] No color buffer available");
    return;
  }

  if (!m_externalBuffer) {
    vsr::core::logError("[CopyToColorBufferPass] No external buffer set");
    return;
  }

  const auto size = getDimensions();
  const size_t totalPixels = size.x * size.y;

  if (totalPixels != m_externalBuffer->size() / 4) {
    vsr::core::logError(
        "[CopyToColorBufferPass] Mismatched dimensions, skipping copy");
    return;
  }

  detail::memcpy_(b.color, m_externalBuffer->data(), totalPixels * 4);
}

} // namespace vsr::rendering
