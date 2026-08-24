// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/objects/Geometry.hpp"
#include "vsr/scene/Scene.hpp"

namespace vsr::scene {

Geometry::Geometry(Token stype) : Object(ANARI_GEOMETRY, stype)
{
  if (subtype() == tokens::geometry::cone
      || subtype() == tokens::geometry::cylinder) {
    addParameter("caps")
        .setValue("none")
        .setDescription("caps type: none, first, second, both")
        .setStringValues({"none", "first", "second", "both"});
  }
}

ObjectPoolRef<Geometry> Geometry::self() const
{
  return scene() ? scene()->getObject<Geometry>(index())
                 : ObjectPoolRef<Geometry>{};
}

anari::Object Geometry::makeANARIObject(anari::Device d) const
{
  return anari::newObject<anari::Geometry>(d, subtype().c_str());
}

namespace tokens::geometry {

const Token cone = "cone";
const Token curve = "curve";
const Token cylinder = "cylinder";
const Token isosurface = "isosurface";
const Token neural = "neural";
const Token quad = "quad";
const Token sdf = "sdf";
const Token sphere = "sphere";
const Token triangle = "triangle";

} // namespace tokens::geometry

} // namespace vsr::scene
