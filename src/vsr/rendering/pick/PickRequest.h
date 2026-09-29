// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/VSRMath.hpp"
// vsr_scene
#include "vsr/scene/ObjectId.hpp"
// vsr_rendering
#include "vsr/rendering/view/CameraView.h"
// std
#include <cstdint>
#include <optional>

namespace vsr::rendering {

/*
 * Raw channel values of one pixel, as produced by a pickable source. Fields
 * a source cannot produce keep their "background" value.
 */
struct PickSample
{
  float depth{vsr::math::inf};
  uint32_t objectId{vsr::scene::NO_OBJECT_ID};
  uint32_t instanceId{~0u};
  uint32_t primitiveId{~0u};
};

/*
 * Capability of an Image Source that can answer Pick Requests: render one
 * synchronous frame with depth and IDs and report a single pixel, without
 * changing the image the pipeline displays.
 */
struct PickableSource
{
  virtual ~PickableSource() = default;

  // Size of the image pixels are addressed in.
  virtual vsr::math::uint2 pickImageSize() const = 0;
  // 'pixel' is within pickImageSize(), row 0 at the bottom (ANARI order).
  // Returns nothing when no frame could be rendered.
  virtual std::optional<PickSample> renderPickSample(
      vsr::math::uint2 pixel) = 0;
};

/*
 * A one-off query for what lies under a pixel. Pixels outside the image are
 * clamped to its border. Give 'view' to get the world-space hit position.
 */
struct PickRequest
{
  vsr::math::uint2 pixel{0, 0}; // row 0 at the bottom (ANARI order)
  std::optional<CameraView> view;
};

struct PickHit
{
  float depth{vsr::math::inf}; // along the pixel's ray; inf if none
  std::optional<vsr::scene::ObjectIdentity> object;
  uint32_t instanceId{~0u};
  uint32_t primitiveId{~0u};
  // Only with a view and a finite depth.
  std::optional<vsr::math::float3> position;
};

// Returns nothing on a miss (no object and no depth) or when the source could
// not render.
std::optional<PickHit> pick(PickableSource &source, const PickRequest &request);

} // namespace vsr::rendering
