// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// vsr_core
#include "vsr/core/VSRMath.hpp"
// vsr_rendering
#include "vsr/rendering/view/CameraView.h"
// std
#include <optional>

namespace vsr::rendering {

/*
 * Draws the Box Outline (12 edges) of a single world-space AABB into the
 * color buffer through the given camera view. One box per pass instance;
 * hide it via setEnabled(false). Lines are depth-tested against the depth
 * buffer by default and silently fall back to an overlay when no depth
 * buffer is present.
 */
struct BoxOutlineRenderPass : public ImagePass
{
  BoxOutlineRenderPass();
  ~BoxOutlineRenderPass() override;
  const char *name() const override;
  ImageChannels requiredChannels() const override;

  void setBox(const vsr::math::box3 &box);
  // Nothing is drawn without a view. For orthographic views, fragment depth
  // is measured from the eye plane along 'dir' (see CameraView).
  void setView(const std::optional<CameraView> &view);
  void setColor(const vsr::math::float4 &color);
  void setWidth(uint32_t width);
  void setDepthTestEnabled(bool enabled);

 private:
  void render(ImageBuffers &b, FrameState &frame) override;

  vsr::math::box3 m_box{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
  std::optional<CameraView> m_view;
  vsr::math::float4 m_color{0.8f, 0.8f, 0.8f, 1.f};
  uint32_t m_width{1};
  bool m_depthTestEnabled{true};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *BoxOutlineRenderPass::name() const
{
  return "Box Outline";
}

} // namespace vsr::rendering
