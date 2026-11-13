// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/images/ImageCache.hpp"
// vsr_scene
#include "vsr/scene/objects/Material.hpp"
#include "vsr/scene/objects/Sampler.hpp"
// catch
#include "catch.hpp"

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
