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

  void setHDREnabled(bool enabled);
  float currentExposure() const;

 private:
  void render(ImageBuffers &b, int stageId) override;

  // Opaque persistent device scratch for the CUDA luminance reduction
  // (created lazily on first use; empty when built without CUDA).
  struct Scratch;
  std::unique_ptr<Scratch> m_scratch;

  bool m_hdrEnabled{false};
  bool m_hasExposure{false};
  float m_currentExposure{0.f};
  float m_response{0.15f};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *AutoExposurePass::name() const
{
  return "Auto Exposure";
}

} // namespace vsr::rendering
