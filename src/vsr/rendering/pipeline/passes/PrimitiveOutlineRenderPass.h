// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// vsr_core
#include "vsr/core/VSRMath.hpp"

namespace vsr::rendering {

struct PrimitiveOutlineRenderPass : public ImagePass
{
  PrimitiveOutlineRenderPass();
  ~PrimitiveOutlineRenderPass() override;
  const char *name() const override;

  void setOutlineColor(const vsr::math::float4 &color);
  void setThickness(uint32_t thickness);

 private:
  void render(ImageBuffers &b, int stageId) override;

  vsr::math::float4 m_outlineColor{0.8f, 0.8f, 0.8f, 1.f};
  uint32_t m_thickness{1};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *PrimitiveOutlineRenderPass::name() const
{
  return "Primitive Outline";
}

} // namespace vsr::rendering
