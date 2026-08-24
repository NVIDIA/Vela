// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"
#include "vsr/ui/imgui/windows/Window.h"

#include <functional>
#include <string>

namespace vsr::scivis_studio {

struct ShotEditor : public vsr::ui::imgui::Window
{
  ShotEditor(vsr::ui::imgui::Application *app,
      ProjectContext *projectContext,
      std::function<void()> onRender);
  ~ShotEditor() override;

  void buildUI() override;

 private:
  bool inputText(const char *label, std::string &value, size_t capacity = 512);
  void buildUI_deviceSelector(Shot &shot);
  void buildUI_rendererSelector(Shot &shot);
  void buildUI_lightRigSelector(Shot &shot);
  void buildUI_cameraRigSelector(Shot &shot);

  ProjectContext *m_projectContext{nullptr};
  std::function<void()> m_onRender;
  std::string m_rendererLoadAttemptedLibrary;
};

} // namespace vsr::scivis_studio
