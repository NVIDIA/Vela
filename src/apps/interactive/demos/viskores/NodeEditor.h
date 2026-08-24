// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// imnodes
#include <imnodes.h>
// viskores_graph
#include <viskores_graph/ExecutionGraph.h>

#include "NodeInfoWindow.h"

namespace vsr::viskores_graph {

namespace graph = viskores::graph;

class NodeEditor : public vsr::ui::imgui::Window
{
 public:
  NodeEditor(vsr::ui::imgui::Application *app,
      graph::ExecutionGraph *graph,
      vsr::viskores_graph::NodeInfoWindow *nodeInfoWindow);

  void buildUI() override;
  void updateNodeSummary();

 private:
  void contextMenu();
  void contextMenuPin();

  void editor_Node(graph::Node *n);

  int m_summarizedNodeID{-1};
  int m_prevNumSelectedNodes{-1};
  int m_pinHoverId{-1};

  bool m_contextMenuVisible{false};
  bool m_contextPinMenuVisible{false};
  graph::ExecutionGraph *m_graph{nullptr};
  graph::TimeStamp m_lastGraphChange{};
  vsr::viskores_graph::NodeInfoWindow *m_nodeInfoWindow{nullptr};
};

} // namespace vsr::viskores_graph
