// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"
#include "vsr/ui/imgui/windows/Window.h"

#include <cstdint>
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
  bool inputSize(const char *label, uint32_t &value);
  // Each edits `shot` (the frame's draft) and reports whether it did.
  bool buildUI_deviceSelector(Shot &shot);
  bool buildUI_rendererSelector(Shot &shot);
  bool buildUI_lightRigSelector(Shot &shot);
  bool buildUI_cameraRigSelector(Shot &shot);

  ProjectContext *m_projectContext{nullptr};
  std::function<void()> m_onRender;
  std::string m_rendererLoadAttemptedLibrary;
};

} // namespace vsr::scivis_studio
