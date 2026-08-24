// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/objects/Volume.hpp"
#include "vsr/scene/Scene.hpp"

namespace vsr::scene {

Volume::Volume(Token stype) : Object(ANARI_VOLUME, stype)
{
  addParameter("visible").setValue(true).setDescription(
      "whether the volume is visible in the scene");
  if (stype == tokens::volume::transferFunction1D) {
    addParameter("color")
        .setValue(float3{1.f})
        .setUsage(ParameterUsageHint::COLOR)
        .setDescription("transfer function color");
    addParameter("opacity").setValue(1.f).setDescription(
        "transfer function opacity");
    addParameter("unitDistance")
        .setValue(1.f)
        .setDescription(
            "distance after which a 'opacity' fraction of light traveling "
            "through the volume is absorbed")
        .setMin(0.f);
    float2 defaultValueRange{0.f, 1.f};
    addParameter("valueRange")
        .setValue({ANARI_FLOAT32_BOX1, &defaultValueRange})
        .setDescription("transfer function value range");

    float2 defaultOpacityControlPoints[2]{
        float2(0.f),
        float2(1.f),
    };
    setMetadataArray("opacityControlPoints",
        ANARI_FLOAT32_VEC2,
        defaultOpacityControlPoints,
        2);
  }
}

ObjectPoolRef<Volume> Volume::self() const
{
  return scene() ? scene()->getObject<Volume>(index())
                 : ObjectPoolRef<Volume>{};
}

anari::Object Volume::makeANARIObject(anari::Device d) const
{
  return anari::newObject<anari::Volume>(d, subtype().c_str());
}

namespace tokens::volume {

const Token structuredRegular = "structuredRegular";
const Token structuredRectilinear = "structuredRectilinear";
const Token transferFunction1D = "transferFunction1D";

} // namespace tokens::volume

} // namespace vsr::scene
