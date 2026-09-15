// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Object.hpp"

namespace vsr::scene {

/*
 * ANARI Volume object that visualizes volumetric data through a transfer
 * function applied to a SpatialField; subtype selects the volume representation.
 *
 * Example:
 *   auto vol = scene.createObject<Volume>(tokens::volume::transferFunction1D);
 *   vol->setParameter("value", fieldRef);
 */
struct Volume : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Volume);

  Volume(Token subtype = tokens::unknown);
  virtual ~Volume() = default;

  ObjectPoolRef<Volume> self() const;

  // Give a transferFunction1D volume the RGBA sample Array its "color"
  // parameter is meant to hold, unless it already holds one. Idempotent, and
  // a no-op for every other subtype. Creating the Array is a structural act
  // no thin client may perform, so a volume reaches every consumer with one
  // already bound rather than acquiring one on first edit (ADR 0037).
  void ensureColorArray();

  anari::Object makeANARIObject(anari::Device d) const override;
};

using VolumeRef = ObjectPoolRef<Volume>;

namespace tokens::volume {

extern const Token structuredRegular;
extern const Token structuredRectilinear;
extern const Token transferFunction1D;

} // namespace tokens::volume

} // namespace vsr::scene
