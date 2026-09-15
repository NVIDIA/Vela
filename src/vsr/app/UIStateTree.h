// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::app {

// The children of a UI-state tree: the {windows, layout, settings} subtree
// the imgui Application saves (saveUIStateTree) and applies
// (applyUIStateTree) -- each window's settings, the ImGui dock layout, the
// application settings. The SciVis Studio project manifest carries the same
// three children beside the project, and its model library has no UI
// dependency, which is why the names are spelled here rather than beside
// the Application.
constexpr const char *UI_STATE_WINDOWS = "windows";
constexpr const char *UI_STATE_LAYOUT = "layout";
constexpr const char *UI_STATE_SETTINGS = "settings";

} // namespace vsr::app
