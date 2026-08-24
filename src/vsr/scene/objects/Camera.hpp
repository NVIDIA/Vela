// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/ObjectUsePtr.hpp"

namespace vsr::scene {

/*
 * ANARI Camera object that defines the viewpoint and projection used during
 * rendering; subtype selects perspective, orthographic, or omnidirectional.
 *
 * Example:
 *   auto cam = scene.createObject<Camera>(tokens::camera::perspective);
 *   cam->setParameter("fovy", 0.785f); // 45 degrees
 */
struct Camera : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Camera);

  Camera(Token subtype = tokens::unknown);
  virtual ~Camera() override = default;

  ObjectPoolRef<Camera> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;
};

using CameraRef = ObjectPoolRef<Camera>;
using CameraAppRef = ObjectUsePtr<Camera, Object::UseKind::APP>;

namespace tokens::camera {

extern const Token perspective;
extern const Token orthographic;
extern const Token omnidirectional;

} // namespace tokens::camera

} // namespace vsr::scene
