// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/scene/Object.hpp"
// std
#include <functional>

namespace vsr::rendering {

using RenderIndexFilterFcn = std::function<bool(const vsr::scene::Object *)>;

} // namespace vsr::rendering
