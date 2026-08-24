// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Object.hpp"

namespace vsr::scene {

/*
 * ANARI Geometry object that describes renderable geometry (triangles, spheres,
 * curves, etc.) through its parameter map and a device-specific subtype token.
 *
 * Example:
 *   auto g = scene.createObject<Geometry>(tokens::geometry::sphere);
 *   g->setParameter("primitive.radius", radiiArray);
 */
struct Geometry : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Geometry);

  Geometry(Token subtype = tokens::unknown);
  virtual ~Geometry() = default;

  ObjectPoolRef<Geometry> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;
};

using GeometryRef = ObjectPoolRef<Geometry>;

namespace tokens::geometry {

extern const Token cone;
extern const Token curve;
extern const Token cylinder;
extern const Token isosurface;
extern const Token neural;
extern const Token quad;
extern const Token sdf;
extern const Token sphere;
extern const Token triangle;
} // namespace tokens::geometry

} // namespace vsr::scene
