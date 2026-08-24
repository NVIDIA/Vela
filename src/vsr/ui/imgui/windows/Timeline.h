// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_app
#include "vsr/app/Context.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/Window.h"
// std
#include <set>
#include <string>

namespace vsr::ui::imgui {

struct Timeline : public Window
{
  Timeline(Application *app, const char *name = "Timeline");

  void buildUI() override;

 private:
  void buildUI_transport();
  void buildUI_canvas();

  // Canvas state //
  float m_pixelsPerFrame{8.f};
  float m_canvasScrollX{0.f};
  std::set<size_t> m_selectedTracks;
};

} // namespace vsr::ui::imgui
