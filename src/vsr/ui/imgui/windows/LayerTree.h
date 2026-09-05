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
  // How much the widget may change. Full is the default. NoLayerAddRemove
  // keeps the layer "new"/"delete" buttons disabled for an application that
  // owns its layers. ReadOnly keeps selection, expand/collapse and hover but
  // hides or disables every affordance that mutates the scene or the layer
  // structure; the remote client uses it, where layer structure is
  // server-push-only (see the SciVis Studio client-server spec).
  enum class EditMode
  {
    Full,
    NoLayerAddRemove,
    ReadOnly
  };

  LayerTree(Application *app, const char *name = "Layers");
  void buildUI() override;

  void setEditMode(EditMode mode);

 private:
  bool canEdit() const; // not ReadOnly
  bool canAddRemoveLayers() const; // Full

  void buildUI_layerHeader();
  void buildUI_tree();
  void buildUI_activateObjectSceneMenu();
  void buildUI_handleSelection();
  void buildUI_clipboardShortcuts();
  void buildUI_objectSceneMenu();
  // The context-menu items that mutate the scene or layer structure; not
  // emitted in ReadOnly. Returns true when an item added a node, so the
  // caller clears the selection once the popup has ended.
  bool buildUI_mutatingMenuItems(
      vsr::scene::Layer &layer, vsr::scene::LayerNodeRef menuNode);
  void buildUI_newLayerSceneMenu();
  void buildUI_setActiveLayersSceneMenus();

  std::vector<vsr::scene::LayerNodeRef> computeSelectionRange(
      vsr::scene::Layer &layer,
      const vsr::scene::LayerNodeRef &anchor,
      const vsr::scene::LayerNodeRef &target);

  std::vector<vsr::scene::LayerNodeRef> copyNodesTo(
      vsr::scene::LayerNodeRef targetParent,
      const std::vector<vsr::scene::LayerNodeRef> &sourceNodes,
      bool cutOperation);

  bool isValidDropTarget(vsr::scene::Layer &layer,
      vsr::scene::LayerNodeRef targetParent,
      const vsr::scene::LayerNodeRef *sourceNodes,
      size_t count) const;

  // Data //

  EditMode m_editMode{EditMode::Full};
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
