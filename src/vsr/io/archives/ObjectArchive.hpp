// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/archives/ArchiveValidation.hpp"

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::scene {
struct Object;
struct Scene;
} // namespace vsr::scene

namespace vsr::io {

bool serialize_ObjectArchive(
    const scene::Object &object, core::DataNode &archive);
ArchiveValidationResult validate_ObjectArchive(core::DataNode &archive);
scene::Object *deserialize_ObjectArchive(scene::Scene &scene,
    core::DataNode &archive,
    ArchiveValidationResult *validation = nullptr);

bool save_ObjectArchive(const scene::Object &object, const char *filename);
scene::Object *load_ObjectArchive(scene::Scene &scene,
    const char *filename,
    ArchiveValidationResult *validation = nullptr);

} // namespace vsr::io
