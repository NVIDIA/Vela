// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Scene.hpp"

namespace vsr::io::detail {

using namespace vsr::scene;

// Build an image2D sampler holding a `size`x`size` gray checkerboard, mapped
// through "attribute0" with nearest filtering and clamped wrapping.
SamplerRef makeCheckerboardTexture(Scene &scene, int size);

} // namespace vsr::io::detail
