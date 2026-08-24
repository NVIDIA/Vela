// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "NodeInfoWindow.h"

namespace vsr::viskores_graph {

NodeInfoWindow::NodeInfoWindow(vsr::ui::imgui::Application *app)
    : vsr::ui::imgui::Window(app, "Node Info")
{}

void NodeInfoWindow::setText(std::string text)
{
  m_text = text;
}

void NodeInfoWindow::buildUI()
{
  ImGui::TextWrapped("%s", m_text.c_str());
}

} // namespace vsr::viskores_graph
