// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::app {

// Node names shared by the files the imgui Application writes:
//   - UI State (docs/adr/0040): {windows, layout} -- each window's
//     presentation settings and the ImGui dock layout.
//   - an Application Dump: {windows} -- each window's scene settings. Dumps
//     and SciVis Studio manifests written before ADR 0040 also carry layout
//     and settings, which readers now ignore.
//   - Application Preferences: {settings} -- font scale and UI rounding.
constexpr const char *UI_STATE_WINDOWS = "windows";
constexpr const char *UI_STATE_LAYOUT = "layout";
constexpr const char *UI_STATE_SETTINGS = "settings";

} // namespace vsr::app
