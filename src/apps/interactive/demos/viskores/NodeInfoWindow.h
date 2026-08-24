// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>

namespace vsr::viskores_graph {

class NodeInfoWindow : public vsr::ui::imgui::Window
{
 public:
  NodeInfoWindow(vsr::ui::imgui::Application *app);
  ~NodeInfoWindow() override = default;

  void setText(std::string text);

  void buildUI() override;

 private:
  std::string m_text{"<no summary>"};
};

} // namespace vsr::viskores_graph
