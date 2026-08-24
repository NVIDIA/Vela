// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Object.hpp"

namespace vsr::scene {

/*
 * ANARI Light object that illuminates the scene; subtype selects the light
 * model (directional, point, HDRI, etc.) and parameters control its properties.
 *
 * Example:
 *   auto light = scene.createObject<Light>(tokens::light::directional);
 *   light->setParameter("direction", float3{0.f, -1.f, 0.f});
 */
struct Light : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Light);

  Light(Token subtype = tokens::unknown);
  virtual ~Light() = default;

  ObjectPoolRef<Light> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;
};

using LightRef = ObjectPoolRef<Light>;

namespace tokens::light {

extern const Token directional;
extern const Token hdri;
extern const Token point;
extern const Token quad;
extern const Token ring;
extern const Token spot;

} // namespace tokens::light

} // namespace vsr::scene
