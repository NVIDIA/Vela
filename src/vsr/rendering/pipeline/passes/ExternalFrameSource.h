// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// std
#include <cstdint>
#include <vector>

namespace vsr::rendering {

/*
 * Image Source that presents a caller-owned RGBA8 frame, such as one received
 * over the network. When no frame is set, or its size does not match the
 * pipeline, every pixel is filled with the fallback color instead.
 *
 * Example:
 *   auto *src = pipeline.setSource<ExternalFrameSource>();
 *   src->setFallbackColor({1.f, 0.f, 0.f, 1.f}); // "disconnected"
 *   src->setFrame(&receivedPixels);
 */
struct ExternalFrameSource : public ImageSource
{
  ExternalFrameSource();
  ~ExternalFrameSource() override;
  const char *name() const override;

  // 'frame' must outlive this source or be reset with setFrame(nullptr).
  void setFrame(const std::vector<uint8_t> *frame);
  void setFallbackColor(const vsr::math::float4 &color);

 private:
  void render(ImageBuffers &b) override;

  const std::vector<uint8_t> *m_frame{nullptr};
  vsr::math::float4 m_fallbackColor{0.f, 0.f, 0.f, 1.f};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *ExternalFrameSource::name() const
{
  return "External Frame";
}

} // namespace vsr::rendering
