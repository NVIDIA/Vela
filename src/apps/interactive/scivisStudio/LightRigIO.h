// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/io/archives/ArchiveValidation.hpp"
#include "vsr/scene/Layer.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace vsr::core {
struct DataNode;
}

namespace vsr::scene {
struct Scene;
}

namespace vsr::scivis_studio {

vsr::io::ArchiveValidationResult validateLightRigArchive(
    vsr::core::DataNode &archive);
bool saveLightRigArchiveFile(vsr::scene::LayerNodeRef root,
    const std::filesystem::path &file,
    std::string_view displayName);
vsr::scene::LayerNodeRef deserializeLightRigArchive(vsr::scene::Scene &scene,
    vsr::core::DataNode &archive,
    vsr::scene::LayerNodeRef destination,
    std::string *displayName = nullptr);
vsr::scene::LayerNodeRef loadLightRigArchiveFile(vsr::scene::Scene &scene,
    const std::filesystem::path &file,
    vsr::scene::LayerNodeRef destination,
    std::string *displayName = nullptr);

} // namespace vsr::scivis_studio
