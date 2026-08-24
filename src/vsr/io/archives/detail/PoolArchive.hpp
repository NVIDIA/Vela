// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/archives/ArchiveValidation.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <string_view>

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::io::detail {

bool serializePoolArchive(const scene::Scene &scene,
    core::DataNode &archive,
    anari::DataType objectType,
    std::string_view poolName,
    std::string_view schema);
ArchiveValidationResult validatePoolArchive(core::DataNode &archive,
    anari::DataType objectType,
    std::string_view poolName,
    std::string_view schema);
bool deserializePoolArchive(scene::Scene &scene,
    core::DataNode &archive,
    anari::DataType objectType,
    std::string_view poolName,
    std::string_view schema,
    ArchiveValidationResult *validation);

} // namespace vsr::io::detail
