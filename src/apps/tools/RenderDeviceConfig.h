// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/Object.hpp"
// std
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::render_cli {

using RendererAssignments = std::vector<std::pair<std::string, std::string>>;

std::string resolveDevice(const std::optional<std::string> &explicitDevice,
    const std::string &savedDevice);

vsr::scene::Object resolveRenderer(const std::string &device,
    const std::string &savedDevice,
    const vsr::scene::Object *savedRenderer,
    const std::optional<std::string> &explicitSubtype,
    const RendererAssignments &assignments);

} // namespace vsr::render_cli
