// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CopyFromColorBufferPass.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::rendering {

CopyFromColorBufferPass::CopyFromColorBufferPass() = default;

CopyFromColorBufferPass::~CopyFromColorBufferPass() = default;

void CopyFromColorBufferPass::setExternalBuffer(std::vector<uint8_t> &buffer)
{
  m_externalBuffer = &buffer;
}

void CopyFromColorBufferPass::render(ImageBuffers &b, int /*stageId*/)
{
  if (!b.color) {
    vsr::core::logError("[CopyFromColorBufferPass] No color buffer available");
    return;
  }

  if (!m_externalBuffer) {
    vsr::core::logError("[CopyFromColorBufferPass] No external buffer set");
    return;
  }

  const auto size = getDimensions();
  const size_t totalPixels = size.x * size.y;

  if (totalPixels == 0) {
    vsr::core::logError("[CopyFromColorBufferPass] Invalid dimensions");
    return;
  }

  m_externalBuffer->resize(totalPixels * 4); // RGBA8
  detail::memcpy_(m_externalBuffer->data(), b.color, totalPixels * 4);
}

} // namespace vsr::rendering
