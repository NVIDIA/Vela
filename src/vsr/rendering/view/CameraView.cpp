// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/view/CameraView.h"
// std
#include <cmath>

namespace vsr::rendering {

CameraView CameraView::perspective(const vsr::math::float3 &eye,
    const vsr::math::float3 &dir,
    const vsr::math::float3 &up,
    float fovy)
{
  CameraView v;
  v.kind = Kind::PERSPECTIVE;
  v.eye = eye;
  v.dir = dir;
  v.up = up;
  v.fovy = fovy;
  return v;
}

CameraView CameraView::orthographic(const vsr::math::float3 &eye,
    const vsr::math::float3 &dir,
    const vsr::math::float3 &up,
    float height)
{
  CameraView v;
  v.kind = Kind::ORTHOGRAPHIC;
  v.eye = eye;
  v.dir = dir;
  v.up = up;
  v.height = height;
  return v;
}

float effectiveAspect(const CameraView &view, float imageAspect)
{
  return view.aspect > 0.f ? view.aspect : imageAspect;
}

Ray primaryRay(
    const CameraView &view, const vsr::math::float2 &screen, float imageAspect)
{
  using namespace vsr::math;

  const float aspect = effectiveAspect(view, imageAspect);
  const float3 dir = normalize(view.dir);
  const float3 right = normalize(cross(dir, view.up));
  const float3 up = cross(right, dir);
  const float2 offset = screen - float2(0.5f); // image center at 0

  Ray ray;
  if (view.kind == CameraView::Kind::PERSPECTIVE) {
    const float planeHeight = 2.f * std::tan(0.5f * view.fovy);
    const float planeWidth = planeHeight * aspect;
    ray.origin = view.eye;
    ray.direction = normalize(
        dir + offset.x * planeWidth * right + offset.y * planeHeight * up);
  } else {
    const float planeWidth = view.height * aspect;
    ray.origin =
        view.eye + offset.x * planeWidth * right + offset.y * view.height * up;
    ray.direction = dir;
  }
  return ray;
}

} // namespace vsr::rendering
