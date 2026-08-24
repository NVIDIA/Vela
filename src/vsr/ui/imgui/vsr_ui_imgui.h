// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/scene/Scene.hpp"
// imgui
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace vsr::ui {

constexpr float INDENT_AMOUNT = 25.f;

void buildUI_object(vsr::scene::Object &o,
    vsr::scene::Scene &scene,
    bool useTableForParameters = false,
    int level = 0);
bool buildUI_parameter(vsr::scene::Object &o,
    vsr::scene::Parameter &p,
    vsr::scene::Scene &scene,
    bool asTable = false);
size_t buildUI_objects_menulist(
    const vsr::scene::Scene &scene, anari::DataType &type);

void tooltipForPreviousItem(const char *text, bool showWhenDisabled = true);

} // namespace vsr::ui
