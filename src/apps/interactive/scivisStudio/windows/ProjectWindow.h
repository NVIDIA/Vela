// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"
#include "vsr/ui/imgui/windows/Window.h"

namespace vsr::scivis_studio {

struct ProjectWindow : public vsr::ui::imgui::Window
{
  ProjectWindow(
      vsr::ui::imgui::Application *app, ProjectContext *projectContext);
  ~ProjectWindow() override;

  void buildUI() override;

 private:
  ProjectContext *m_projectContext{nullptr};
};

} // namespace vsr::scivis_studio
