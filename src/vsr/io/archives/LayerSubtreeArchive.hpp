// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/archives/ArchiveValidation.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::io {

bool serialize_LayerSubtreeArchive(
    scene::LayerNodeRef subtree, core::DataNode &archive);
ArchiveValidationResult validate_LayerSubtreeArchive(core::DataNode &archive);
scene::LayerNodeRef deserialize_LayerSubtreeArchive(
    scene::LayerNodeRef destination, core::DataNode &archive);

bool save_LayerSubtreeArchive(
    scene::LayerNodeRef subtree, const char *filename);
scene::LayerNodeRef load_LayerSubtreeArchive(
    scene::LayerNodeRef destination, const char *filename);

} // namespace vsr::io
