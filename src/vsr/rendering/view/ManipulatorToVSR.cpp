// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/view/ManipulatorToVSR.hpp"

namespace vsr::rendering {

// An orthographic camera object's image height per unit of orbit distance.
constexpr float ORTHOGRAPHIC_HEIGHT_PER_DISTANCE = 0.75f;

void updateCameraObject(vsr::scene::Camera &c,
    const Manipulator &m,
    bool includeManipulatorMetadata)
{
  c.beginParameterBatch();

  c.setParameter("direction", m.dir());
  c.setParameter("up", m.up());

  if (c.subtype() == scene::tokens::camera::orthographic) {
    c.setParameter("position", m.eye_FixedDistance());
    c.setParameter("height", m.distance() * ORTHOGRAPHIC_HEIGHT_PER_DISTANCE);
  } else {
    c.setParameter("position", m.eye());
  }

  if (includeManipulatorMetadata) {
    c.setMetadataValue("manipulator.at", m.at());
    c.setMetadataValue("manipulator.distance", m.distance());
    c.setMetadataValue("manipulator.fixedDistance", m.fixedDistance());
    c.setMetadataValue("manipulator.azel", m.azel());
    c.setMetadataValue("manipulator.up", int(m.axis()));
    c.setMetadataValue("manipulator.mode", int(m.mode()));
  }

  c.endParameterBatch();
}

void updateManipulatorFromCamera(Manipulator &m, const vsr::scene::Camera &c)
{
  auto at = c.getMetadataValue("manipulator.at").getValueOr(m.at());
  auto d = c.getMetadataValue("manipulator.distance").getValueOr(m.distance());
  auto azel = c.getMetadataValue("manipulator.azel").getValueOr(m.azel());
  auto up = c.getMetadataValue("manipulator.up").getValueOr(int(m.axis()));
  auto mode = c.getMetadataValue("manipulator.mode")
                  .getValueOr(int(ManipulatorMode::Orbit));
  auto fd = c.getMetadataValue("manipulator.fixedDistance")
                .getValueOr(m.fixedDistance());

  CameraPose pose;
  pose.lookat = at;
  pose.azeldist = {azel.x, azel.y, d};
  pose.upAxis = up;
  pose.mode = mode;
  m.setConfig(pose);
  m.setFixedDistance(fd);
}

void updateManipulatorFromCameraPose(
    Manipulator &m, const vsr::scene::Camera &c)
{
  const auto *position = c.parameter("position");
  const auto *direction = c.parameter("direction");
  const auto *up = c.parameter("up");
  if (!position || !direction || !up)
    return;
  if (!position->value().is<vsr::math::float3>()
      || !direction->value().is<vsr::math::float3>()
      || !up->value().is<vsr::math::float3>())
    return;
  const auto eye = position->value().get<vsr::math::float3>();
  const auto dir = direction->value().get<vsr::math::float3>();
  const auto upVector = up->value().get<vsr::math::float3>();
  // An orthographic camera's position is the eye at the fixed distance and
  // its height the orbit distance's image: adopt both, or updateCameraObject
  // would write the manipulator's own back over the edit.
  if (c.subtype() == scene::tokens::camera::orthographic) {
    if (const auto height = c.parameterValueAs<float>("height");
        height && *height > 0.f) {
      m.setFixedDistancePose(
          eye, dir, upVector, *height / ORTHOGRAPHIC_HEIGHT_PER_DISTANCE);
      return;
    }
  }
  m.setPose(eye, dir, upVector);
}

} // namespace vsr::rendering
