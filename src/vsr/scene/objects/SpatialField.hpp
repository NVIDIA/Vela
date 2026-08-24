// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/ObjectUsePtr.hpp"

namespace vsr::scene {

struct Scene;

/*
 * ANARI SpatialField object that holds the raw scalar or vector field data
 * (e.g. structured-regular, NanoVDB) consumed by a Volume for rendering.
 *
 * Example:
 *   auto field = scene.createObject<SpatialField>(tokens::spatial_field::structuredRegular);
 *   field->setParameter("data", dataArray);
 */
struct SpatialField : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(SpatialField);

  SpatialField(Token subtype = tokens::unknown);
  virtual ~SpatialField() = default;

  ObjectPoolRef<SpatialField> self() const;

  anari::Object makeANARIObject(anari::Device d) const override;

  vsr::math::float2 computeValueRange();
};

using SpatialFieldRef = ObjectPoolRef<SpatialField>;
using SpatialFieldAppRef = ObjectUsePtr<SpatialField, Object::UseKind::APP>;

namespace tokens::spatial_field {

extern const Token structuredRegular;
extern const Token structuredRectilinear;
extern const Token unstructured;
extern const Token amr;
extern const Token nanovdb;
extern const Token nanovdbRectilinear;

} // namespace tokens::spatial_field

} // namespace vsr::scene
