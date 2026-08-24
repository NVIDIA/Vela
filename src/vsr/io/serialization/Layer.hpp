// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::io {

void serialize_Layer(const scene::Layer &layer, core::DataNode &node);
void serialize_LayerSubtree(
    const scene::Layer &layer, scene::LayerNodeRef start, core::DataNode &node);
void deserialize_Layer(
    core::DataNode &node, scene::Layer &layer, scene::Scene &scene);

} // namespace vsr::io
