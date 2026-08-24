// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/FlatMap.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <string>
#include <vector>

namespace vsr::io::usd {

/*
 * What resolving a gprim needs that it cannot read off the resolved prim --
 * which is to say, everything an Import decided that does not change over
 * time. An animation binding carries one of these and replays it, so a scrub
 * reproduces the Import's conversion instead of guessing at it again.
 *
 * `uvNamesByPart` and `slotPrimvarsByPart` together replay the whole attribute
 * assignment. Which primvar a Part's material reads as texture coordinates
 * decides `attribute0`, and a scrub must not re-resolve materials to find that
 * out; the remaining slots went to whichever primvars the prim happened to
 * carry, so naming them is what keeps a primvar that appears or disappears
 * mid-sequence from silently re-slotting the others. An absent uv entry means
 * the conventional `st`; an absent slot entry means the resolve is free to
 * assign, which is what the Import itself does on its first pass.
 *
 * Deliberately free of OpenUSD types, so the animation bindings that carry one
 * still declare themselves in builds without USD.
 */
struct GeometryResolveOptions
{
  // Baked into the emitted vertex data; identity for everything but
  // Prototype-internal geometry (ADR 0016).
  vsr::math::mat4 bakeXform{vsr::math::IDENTITY_MAT4};
  bool refine{false};
  int refinementLevel{2};
  vsr::core::FlatMap<std::string, std::string> uvNamesByPart;
  vsr::core::FlatMap<std::string, std::vector<std::string>> slotPrimvarsByPart;
};

} // namespace vsr::io::usd
