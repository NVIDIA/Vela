// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr
#include "vsr/core/FlatMap.hpp"

namespace vsr::ui::imgui {

struct ImportFileDialog;

struct LayerTree : public Window
{
  LayerTree(Application *app, const char *name = "Layers");
  void buildUI() override;

  void setEnableAddRemoveLayers(bool enable);

  // Puts the whole widget in read-only mode: selection, expand/collapse, and
  // hover still work, but every affordance that mutates the scene or layer
  // structure is hidden or disabled. Used by the remote client, where layer
  // structure is server-push-only (see the SciVis Studio client-server spec).
  void setReadOnly(bool readOnly);

 private:
  void buildUI_layerHeader();
  void buildUI_tree();
  void buildUI_activateObjectSceneMenu();
  void buildUI_handleSelection();
  void buildUI_objectSceneMenu();
  void buildUI_newLayerSceneMenu();
  void buildUI_setActiveLayersSceneMenus();

  std::vector<vsr::scene::LayerNodeRef> computeSelectionRange(
      vsr::scene::Layer &layer,
      const vsr::scene::LayerNodeRef &anchor,
      const vsr::scene::LayerNodeRef &target);

  std::vector<vsr::scene::LayerNodeRef> copyNodesTo(
      vsr::scene::LayerNodeRef targetParent,
      const std::vector<vsr::scene::LayerNodeRef>& sourceNodes,
      bool cutOperation
  );

  bool isValidDropTarget(
      vsr::scene::Layer& layer,
      vsr::scene::LayerNodeRef targetParent,
      const vsr::scene::LayerNodeRef* sourceNodes,
      size_t count
  ) const;

  // Data //

  bool m_enableAddRemove{true};
  bool m_readOnly{false};
  size_t m_hoveredNode{VSR_INVALID_INDEX};
  size_t m_menuNode{VSR_INVALID_INDEX};
  bool m_activeLayerMenuTriggered{false};
  bool m_editingNodeName{false};
  bool m_menuVisible{false};
  std::vector<int> m_needToTreePop;
  int m_layerIdx{0};
  vsr::scene::LayerNodeRef m_anchorNode;
};

} // namespace vsr::ui::imgui
