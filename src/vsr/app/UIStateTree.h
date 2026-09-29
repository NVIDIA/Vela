// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::app {

// The children of a UI-state tree: the {windows, layout, settings} subtree
// the imgui Application saves (saveUIStateTree) and applies
// (applyUIStateTree) -- each window's settings, the ImGui dock layout, the
// application settings. SciVis Studio project manifests written before
// docs/adr/0040 carry the same three children beside the project; readers
// now ignore them.
constexpr const char *UI_STATE_WINDOWS = "windows";
constexpr const char *UI_STATE_LAYOUT = "layout";
constexpr const char *UI_STATE_SETTINGS = "settings";

} // namespace vsr::app
