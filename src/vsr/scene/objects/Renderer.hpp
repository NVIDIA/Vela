// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/ObjectUsePtr.hpp"

namespace vsr::scene {

/*
 * ANARI Renderer object that encapsulates a device-specific rendering algorithm
 * selected by subtype; stores parameters that control rendering quality and mode.
 *
 * Example:
 *   auto rend = scene.createRenderer(deviceToken, tokens::defaultToken);
 *   rend->setParameter("ambientRadiance", 0.1f);
 */
struct Renderer : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Renderer);

  Renderer() = default;
  Renderer(Token sourceDevice, Token subtype);
  virtual ~Renderer() = default;

  ObjectPoolRef<Renderer> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;

  void setCommonParameterDefaults();
};

using RendererRef = ObjectPoolRef<Renderer>;
using RendererAppRef = ObjectUsePtr<Renderer, Object::UseKind::APP>;

} // namespace vsr::scene
