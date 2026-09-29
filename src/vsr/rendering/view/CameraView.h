// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/VSRMath.hpp"

namespace vsr::rendering {

/*
 * The projection of an ANARI perspective or orthographic camera, as needed to
 * relate image pixels to world space. 'eye' is where rays originate: the
 * camera position for perspective, a point on the ray-origin plane for
 * orthographic (e.g. Manipulator::eye_FixedDistance()).
 *
 * Example:
 *   auto view = CameraView::perspective(m.eye(), m.dir(), m.up(), fovy);
 *   auto ray = primaryRay(view, {0.5f, 0.5f}, width / float(height));
 */
struct CameraView
{
  enum class Kind
  {
    PERSPECTIVE,
    ORTHOGRAPHIC
  };

  static CameraView perspective(const vsr::math::float3 &eye,
      const vsr::math::float3 &dir,
      const vsr::math::float3 &up,
      float fovy);
  static CameraView orthographic(const vsr::math::float3 &eye,
      const vsr::math::float3 &dir,
      const vsr::math::float3 &up,
      float height);

  Kind kind{Kind::PERSPECTIVE};
  vsr::math::float3 eye{0.f, 0.f, 0.f};
  vsr::math::float3 dir{0.f, 0.f, -1.f};
  vsr::math::float3 up{0.f, 1.f, 0.f};
  float fovy{0.f}; // radians, perspective only
  float height{0.f}; // world units, orthographic only
  // Width / height of the camera's image plane, when the camera sets it
  // explicitly (ANARI "aspect"); 0 follows the rendered image.
  float aspect{0.f};
};

struct Ray
{
  vsr::math::float3 origin{0.f, 0.f, 0.f};
  vsr::math::float3 direction{0.f, 0.f, -1.f}; // normalized
};

// The image-plane aspect 'view' renders with into an image of 'imageAspect'
// (width / height).
float effectiveAspect(const CameraView &view, float imageAspect);

// Ray through 'screen', the image position in [0, 1]^2 with (0, 0) at the
// bottom-left corner (ANARI frame row order), for an image of 'imageAspect'.
// ANARI depth along this ray is the distance from 'origin'.
Ray primaryRay(
    const CameraView &view, const vsr::math::float2 &screen, float imageAspect);

} // namespace vsr::rendering
