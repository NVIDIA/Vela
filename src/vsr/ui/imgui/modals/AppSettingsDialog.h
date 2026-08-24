// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"

namespace vsr::ui::imgui {

struct AppSettingsDialog : public Modal
{
  AppSettingsDialog(Application *app);
  ~AppSettingsDialog() override = default;

  void buildUI() override;
  void applySettings();

 private:
  void buildUI_applicationSettings();
  void buildUI_offlineRenderSettings();

  std::vector<vsr::scene::CameraRef> m_menuCameraRefs;
};

} // namespace vsr::ui::imgui
