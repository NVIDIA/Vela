// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// catch
#include "catch.hpp"
// vsr
#include "vsr/io/images/ImageCache.hpp"
#include "vsr/scene/objects/Material.hpp"
#include "vsr/scene/objects/Sampler.hpp"

// The output transform of the sampler a material binds to `parameter`.
vsr::io::OutputTransform boundOutputTransform(
    const vsr::scene::Material &material, const char *parameter);

// Inlined definitions ////////////////////////////////////////////////////////

inline vsr::io::OutputTransform boundOutputTransform(
    const vsr::scene::Material &material, const char *parameter)
{
  auto *sampler =
      material.parameterValueAsObject<vsr::scene::Sampler>(parameter);
  REQUIRE(sampler != nullptr);
  return vsr::io::outputTransformOf(*sampler);
}
