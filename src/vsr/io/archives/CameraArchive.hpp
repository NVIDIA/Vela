// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/archives/ArchiveValidation.hpp"

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::io {

bool serialize_CameraArchive(
    const scene::Scene &scene, core::DataNode &archive);
ArchiveValidationResult validate_CameraArchive(core::DataNode &archive);
bool deserialize_CameraArchive(scene::Scene &scene,
    core::DataNode &archive,
    ArchiveValidationResult *validation = nullptr);
bool save_CameraArchive(const scene::Scene &scene, const char *filename);
bool load_CameraArchive(scene::Scene &scene,
    const char *filename,
    ArchiveValidationResult *validation = nullptr);

} // namespace vsr::io
