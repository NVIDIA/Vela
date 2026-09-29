// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/VSRMath.hpp"
// vsr_rendering
#include "vsr/rendering/view/CameraView.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstdint>
#include <optional>

namespace vsr::rendering {

struct BoxOutlineRenderPass;
struct ImagePipeline;

/*
 * Draws the Box Outline of the rendered world's bounds, depth-tested against
 * the scene. Adds its Box Outline pass to 'pipeline' on construction; the
 * pass lives as long as the pipeline does (until ImagePipeline::clear()).
 * Call update() before each render with the current bounds and view.
 *
 * Example:
 *   WorldBoundsOverlay bounds(pipeline);
 *   bounds.setShown(true);
 *   bounds.update(queryWorldBounds(device, world), makeCameraView(cam, m));
 *   pipeline.render();
 */
struct WorldBoundsOverlay
{
  explicit WorldBoundsOverlay(ImagePipeline &pipeline);

  bool isShown() const;
  vsr::math::float4 color() const;
  uint32_t width() const;

  void setShown(bool shown);
  void setColor(const vsr::math::float4 &color);
  void setWidth(uint32_t width); // pixels, at least 1
  // Draws nothing without bounds (empty world) or view (unsupported camera).
  void update(const std::optional<vsr::math::box3> &bounds,
      const std::optional<CameraView> &view);

 private:
  BoxOutlineRenderPass *m_pass{nullptr};
  bool m_shown{false};
  vsr::math::float4 m_color{0.8f, 0.8f, 0.8f, 1.f};
  uint32_t m_width{1};
};

// The world's "bounds" property; nothing if the device reports none or an
// empty box.
std::optional<vsr::math::box3> queryWorldBounds(
    anari::Device device, anari::World world);

} // namespace vsr::rendering
