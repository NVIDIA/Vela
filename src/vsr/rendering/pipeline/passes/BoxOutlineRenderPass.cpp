// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "BoxOutlineRenderPass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/boxOutline.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/boxOutline.hpp"
#endif
// helium
#include <helium/helium_math.h>
// std
#include <cmath>

namespace vsr::rendering {

BoxOutlineRenderPass::BoxOutlineRenderPass() = default;

BoxOutlineRenderPass::~BoxOutlineRenderPass() = default;

void BoxOutlineRenderPass::setBox(const vsr::math::box3 &box)
{
  m_box = box;
}

void BoxOutlineRenderPass::setView(const std::optional<CameraView> &view)
{
  m_view = view;
}

void BoxOutlineRenderPass::setColor(const vsr::math::float4 &color)
{
  m_color = color;
}

void BoxOutlineRenderPass::setWidth(uint32_t width)
{
  m_width = width;
}

void BoxOutlineRenderPass::setDepthTestEnabled(bool enabled)
{
  m_depthTestEnabled = enabled;
}

ImageChannels BoxOutlineRenderPass::requiredChannels() const
{
  const bool drawn = m_view.has_value();
  return drawn && m_depthTestEnabled ? ImageChannels::DEPTH
                                     : ImageChannels::NONE;
}

void BoxOutlineRenderPass::render(ImageBuffers &b, FrameState & /*frame*/)
{
  if (!b.color || !m_view)
    return;
  const CameraView &v = *m_view;
  const bool orthographic = v.kind == CameraView::Kind::ORTHOGRAPHIC;

  const auto size = dimensions();
  if (size.x == 0 || size.y == 0)
    return;

  const float aspect = effectiveAspect(v, size.x / float(size.y));

  const auto view = linalg::lookat_matrix(v.eye, v.eye + v.dir, v.up);

  // Only the projected xy is consumed downstream (fragment depth comes from
  // the interpolated world position), so near/far need no scene fitting.
  constexpr float near = 0.1f;
  constexpr float far = 1000.f;

  vsr::math::mat4 proj;
  if (!orthographic) {
    const float oneOverTanFov = 1.f / std::tan(v.fovy / 2.f);
    proj = vsr::math::mat4{
        {oneOverTanFov / aspect, 0.f, 0.f, 0.f},
        {0.f, oneOverTanFov, 0.f, 0.f},
        {0.f, 0.f, -(far + near) / (far - near), -1.f},
        {0.f, 0.f, -2.f * far * near / (far - near), 0.f},
    };
  } else {
    const float halfHeight = v.height * 0.5f;
    const float halfWidth = halfHeight * aspect;
    proj = vsr::math::mat4{
        {1.f / halfWidth, 0.f, 0.f, 0.f},
        {0.f, 1.f / halfHeight, 0.f, 0.f},
        {0.f, 0.f, -2.f / (far - near), 0.f},
        {0.f, 0.f, -(far + near) / (far - near), 1.f},
    };
  }

  const auto projView = vsr::math::mul(proj, view);
  const auto color = helium::cvt_color_to_uint32(m_color);
  const float *depth = m_depthTestEnabled ? b.depth : nullptr;

#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    vsr::algorithms::cuda::boxOutline(b.stream,
        m_box,
        projView,
        v.eye,
        v.dir,
        orthographic,
        depth,
        b.color,
        color,
        m_width,
        size.x,
        size.y);
    return;
  }
#endif
  vsr::algorithms::cpu::boxOutline(m_box,
      projView,
      v.eye,
      v.dir,
      orthographic,
      depth,
      b.color,
      color,
      m_width,
      size.x,
      size.y);
}

} // namespace vsr::rendering
