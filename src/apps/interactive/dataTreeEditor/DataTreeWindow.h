// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// vsr_core
#include <vsr/core/DataTree.hpp>
// std
#include <string>
#include <vector>

namespace vsr::datatree_editor {

class DataTreeWindow : public vsr::ui::imgui::Window
{
 public:
  DataTreeWindow(vsr::ui::imgui::Application *app,
      vsr::core::DataTree *tree,
      bool *dirty,
      const std::string *currentFile);
  ~DataTreeWindow() override = default;

  void buildUI() override;

 private:
  // parent is nullptr only for immediate root children (their parent is root)
  void renderNode(vsr::core::DataNode &node, vsr::core::DataNode &parent);
  void renderValueEditor(vsr::core::DataNode &node);
  void applyPendingOps();

  vsr::core::DataTree *m_tree{nullptr};
  bool *m_dirty{nullptr};
  const std::string *m_currentFile{nullptr};

  // Deferred structural edits — target is always the *parent* node.
  // For AddChild: append a child named 'name' to target.
  // For Delete:   remove the child named 'name' from target.
  struct PendingOp
  {
    enum Type { AddChild, Delete } type;
    vsr::core::DataNode *target{nullptr};
    std::string name;
  };
  std::vector<PendingOp> m_pendingOps;

  // State for "Add Child" modal
  vsr::core::DataNode *m_addChildTarget{nullptr};
  std::string m_addChildName;
};

} // namespace vsr::datatree_editor
