// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// This header defines comparison operators for VSR types that don't have them
// in their core headers. These operators are only needed for Sol2's template
// machinery and are intentionally defined here to avoid polluting the core
// library public headers.
//
// Include this header AFTER the VSR headers and BEFORE <sol/sol.hpp>.

#include "vsr/scene/Layer.hpp"
#include "vsr/scene/objects/Array.hpp"
#include "vsr/scene/objects/Camera.hpp"
#include "vsr/scene/objects/Geometry.hpp"
#include "vsr/scene/objects/Light.hpp"
#include "vsr/scene/objects/Material.hpp"
#include "vsr/scene/objects/Sampler.hpp"
#include "vsr/scene/objects/SpatialField.hpp"
#include "vsr/scene/objects/Surface.hpp"
#include "vsr/scene/objects/Volume.hpp"

#include <functional>

namespace vsr::scene {

// Macro to generate all 6 comparison operators for a type.
// These compare by pointer identity, which is what Sol2 needs for usertype
// objects that don't have semantic comparison operators.
// Ordering uses std::less to guarantee a total order across unrelated pointers.
#define VSR_SOL2_COMPARISON_OPS(Type)                                          \
  inline bool operator==(const Type &a, const Type &b)                         \
  {                                                                            \
    return &a == &b;                                                           \
  }                                                                            \
  inline bool operator!=(const Type &a, const Type &b)                         \
  {                                                                            \
    return &a != &b;                                                           \
  }                                                                            \
  inline bool operator<(const Type &a, const Type &b)                          \
  {                                                                            \
    return std::less<const Type *>{}(&a, &b);                                  \
  }                                                                            \
  inline bool operator<=(const Type &a, const Type &b)                         \
  {                                                                            \
    return &a == &b || std::less<const Type *>{}(&a, &b);                      \
  }                                                                            \
  inline bool operator>(const Type &a, const Type &b)                          \
  {                                                                            \
    return std::less<const Type *>{}(&b, &a);                                  \
  }                                                                            \
  inline bool operator>=(const Type &a, const Type &b)                         \
  {                                                                            \
    return &a == &b || std::less<const Type *>{}(&b, &a);                      \
  }

VSR_SOL2_COMPARISON_OPS(Array)
VSR_SOL2_COMPARISON_OPS(Surface)
VSR_SOL2_COMPARISON_OPS(SpatialField)
VSR_SOL2_COMPARISON_OPS(Camera)
VSR_SOL2_COMPARISON_OPS(Geometry)
VSR_SOL2_COMPARISON_OPS(Light)
VSR_SOL2_COMPARISON_OPS(Material)
VSR_SOL2_COMPARISON_OPS(Sampler)
VSR_SOL2_COMPARISON_OPS(Volume)
VSR_SOL2_COMPARISON_OPS(LayerNodeData)

#undef VSR_SOL2_COMPARISON_OPS

} // namespace vsr::scene
