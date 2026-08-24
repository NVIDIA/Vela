// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"

namespace vsr::io {

void serialize_Object(const scene::Object &object,
    core::DataNode &node,
    bool forceArraysAsProxies = false);
void deserialize_Object(core::DataNode &node, scene::Object &object);
void deserialize_Object(scene::Scene &scene, core::DataNode &node);

} // namespace vsr::io
