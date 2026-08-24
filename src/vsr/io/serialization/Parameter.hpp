// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_scene
#include "vsr/scene/Parameter.hpp"

namespace vsr::io {

void serialize_Parameter(
    const scene::Parameter &parameter, core::DataNode &node);
void deserialize_Parameter(core::DataNode &node, scene::Parameter &parameter);

} // namespace vsr::io
