// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/FlatMap.hpp"
#include "vsr/core/VSRMath.hpp"
#include "vsr/core/Token.hpp"
// vsr_io
#include "vsr/io/usd/UsdGeometryResolveOptions.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Geometry.hpp"
// usd
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/usd/sdf/path.h>
// std
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::io::usd {

/*
 * The plain-data half of geometry conversion.
 *
 * Resolving a gprim -- reading its topology and primvars, refining it,
 * triangulating it, expanding and gathering its attributes -- is the part that
 * changes over time. Turning the result into Surfaces, Materials and Arrays is
 * the part that does not. Separating them is what lets an animation binding
 * re-pull a mesh's points, indices and primvars as one consistent set without
 * re-creating the objects around them (ADR 0022).
 *
 * Resolving touches no Scene: a ResolvedGeometry is inert data that can be
 * produced at any Time Code and then either built into new objects or written
 * over existing ones. Writing it back (refillGeometry, below) necessarily does.
 */

// One geometry parameter's worth of resolved data, already expanded, gathered
// and oriented -- whatever lands here binds as authored.
struct ResolvedAttribute
{
  vsr::core::Token parameter;
  anari::DataType type{ANARI_UNKNOWN};
  pxr::VtValue value;

  // Non-empty when this exact buffer also appears on other Parts, which is how
  // every Surface of a subdivided mesh ends up pointing at one position Array
  // rather than a copy each. Keyed on what produced the data, not on the
  // parameter it lands in: two Parts can bind one primvar to different slots.
  std::string sharedKey;

  size_t count() const;
  const void *data() const;
  bool valid() const;
};

// One emitted Surface's geometry. A mesh carrying per-face material subsets
// resolves to several Parts sharing the mesh's vertex data.
struct ResolvedPart
{
  vsr::core::Token subtype;
  std::string name;
  std::vector<ResolvedAttribute> attributes;
  std::vector<std::pair<vsr::core::Token, float>> scalars;

  // Which primvar took each spare attribute slot, in slot order. Reported so
  // an Import can record it and a later resolve can replay it.
  std::vector<std::string> slotPrimvars;

  const ResolvedAttribute *attribute(vsr::core::Token parameter) const;
  bool provides(vsr::core::Token parameter) const;
};

struct ResolvedGeometry
{
  std::vector<ResolvedPart> parts;

  bool valid() const;
  const ResolvedPart *part(const std::string &name) const;
};

// Resolve one gprim at whatever Time Code the scene index is currently set to.
ResolvedGeometry resolveGeometry(
    const pxr::HdSceneIndexBaseRefPtr &sceneIndex,
    const pxr::SdfPath &primPath,
    const pxr::HdSceneIndexPrim &prim,
    const GeometryResolveOptions &options);

// True for the resolved prim types resolveGeometry() handles.
bool isGeometryPrimType(const pxr::TfToken &primType);

// The constant display colour and opacity a prim carries, which is what an
// unmaterialed prim is shaded with so it looks as it does in a reference
// viewer rather than taking VSR's default.
struct DisplayColor
{
  std::optional<vsr::math::float3> color;
  std::optional<float> opacity;
};

DisplayColor readDisplayColor(const pxr::HdSceneIndexPrim &prim);

// True when a gprim carries enough to resolve into anything at all. Asked
// before the objects around a gprim are built, so that a prim yielding no
// geometry does not leave a Material behind that nothing references.
bool geometryWillResolve(const pxr::HdSceneIndexPrim &prim);

/*
 * Arrays shared between the Parts of one gprim, so that a mesh's Surfaces keep
 * pointing at one position Array rather than a copy each -- and, on the common
 * path, so that the one Array is written once per frame rather than once per
 * Part. One of these covers one resolve; it must not outlive it.
 */
struct RefillCache
{
  vsr::core::FlatMap<std::string, scene::ArrayRef> sharedArrays;
};

/*
 * Write a resolved Part over an existing Geometry, reusing every Array whose
 * size and element type still fit and allocating only the ones that do not.
 * This is the whole point of the split: points, indices and primvars arrive
 * together, so the Geometry is never left describing half of one frame and
 * half of another.
 *
 * Returns false when the Part cannot be applied to this Geometry at all --
 * a different subtype, which means the prim changed shape rather than moved.
 */
bool refillGeometry(scene::Scene &scene,
    scene::Geometry &geometry,
    const ResolvedPart &part,
    RefillCache &cache);

} // namespace vsr::io::usd
