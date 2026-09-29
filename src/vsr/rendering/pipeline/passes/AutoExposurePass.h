// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// std
#include <memory>

namespace vsr::rendering {

struct AutoExposurePass : public ImagePass
{
  AutoExposurePass();
  ~AutoExposurePass() override;
  const char *name() const override;
  ImageChannels requiredChannels() const override;

  void setHDREnabled(bool enabled);
  float currentExposure() const;

 private:
  void render(ImageBuffers &b, FrameState &frame) override;

  // Opaque persistent device scratch for the CUDA luminance reduction
  // (created lazily on first use; empty when built without CUDA).
  struct Scratch;
  std::unique_ptr<Scratch> m_scratch;

  bool m_hdrEnabled{false};
  bool m_hasExposure{false};
  float m_currentExposure{0.f};
  float m_response{0.15f};
  // Re-seeds the host sampler's jitter every frame.
  uint32_t m_frameIndex{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *AutoExposurePass::name() const
{
  return "Auto Exposure";
}

} // namespace vsr::rendering
