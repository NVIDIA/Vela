// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/objects/Geometry.hpp"
#include "vsr/scene/objects/Material.hpp"

namespace vsr::scene {

/*
 * ANARI Surface object that pairs a Geometry with a Material; when placed in
 * a Layer it becomes a renderable instance in the scene.
 *
 * Example:
 *   auto surf = scene.createSurface("myMesh", geomRef, matRef);
 *   scene.insertChildObjectNode(layer->root(), surf);
 */
struct Surface : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Surface);

  Surface();
  virtual ~Surface() = default;

  void setGeometry(GeometryRef g);
  void setMaterial(MaterialRef m);

  Geometry *geometry() const;
  Material *material() const;
  ObjectPoolRef<Surface> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;
};

using SurfaceRef = ObjectPoolRef<Surface>;

namespace tokens::surface {

extern const Token geometry;
extern const Token material;

} // namespace tokens::surface

} // namespace vsr::scene
