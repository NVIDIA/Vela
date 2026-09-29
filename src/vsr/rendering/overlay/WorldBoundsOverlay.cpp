// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/overlay/WorldBoundsOverlay.h"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/passes/BoxOutlineRenderPass.h"
// std
#include <algorithm>

namespace vsr::rendering {

WorldBoundsOverlay::WorldBoundsOverlay(ImagePipeline &pipeline)
    : m_pass(pipeline.addPass<BoxOutlineRenderPass>())
{
  m_pass->setEnabled(false);
}

bool WorldBoundsOverlay::isShown() const
{
  return m_shown;
}

vsr::math::float4 WorldBoundsOverlay::color() const
{
  return m_color;
}

uint32_t WorldBoundsOverlay::width() const
{
  return m_width;
}

void WorldBoundsOverlay::setShown(bool shown)
{
  m_shown = shown;
}

void WorldBoundsOverlay::setColor(const vsr::math::float4 &color)
{
  m_color = color;
}

void WorldBoundsOverlay::setWidth(uint32_t width)
{
  m_width = std::max(1u, width);
}

void WorldBoundsOverlay::update(const std::optional<vsr::math::box3> &bounds,
    const std::optional<CameraView> &view)
{
  const bool draw = m_shown && bounds && view;
  m_pass->setEnabled(draw);
  if (!draw)
    return;

  m_pass->setBox(*bounds);
  m_pass->setView(view);
  m_pass->setColor(m_color);
  m_pass->setWidth(m_width);
}

std::optional<vsr::math::box3> queryWorldBounds(
    anari::Device device, anari::World world)
{
  if (!device || !world)
    return {};

  vsr::math::box3 bounds;
  const bool found = anariGetProperty(device,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds,
      sizeof(bounds),
      ANARI_WAIT);
  const bool empty = bounds.lower.x > bounds.upper.x
      || bounds.lower.y > bounds.upper.y || bounds.lower.z > bounds.upper.z;
  if (!found || empty)
    return {};
  return bounds;
}

} // namespace vsr::rendering
