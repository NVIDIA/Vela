// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr_core
#include "vsr/core/FlatMap.hpp"
// std
#include <string>
#include <vector>

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

  // Drop everything the widget remembers about the current scene: the
  // selection anchor, the hovered and context-menu nodes and the chosen
  // layer. Applications that replace the whole scene under a live widget
  // (the SciVis Studio client, whenever a Project Snapshot replaces the
  // Structural Mirror) must call this before the old layers are destroyed:
  // a LayerNodeRef has no generation counter, so a retained one still
  // reports valid() while dangling. Harmless to call at any other time.
  void dropSceneReferences();

 private:
  bool canEdit() const; // not ReadOnly
  bool canAddRemoveLayers() const; // Full

  // Reveal //

  // When the first selected node changed outside this widget (a viewport
  // pick, a demo, a script), switch to its layer and arm m_revealNode so the
  // next tree build expands its ancestors and scrolls it into view.
  void checkForExternalSelectionChange();

  void buildUI_layerHeader();
  void buildUI_filter();
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

  // Filtering //

  bool filterActive() const;
  // Fill m_nodeShown for the layer: a node matches when it passes the
  // type checkboxes and it or an ancestor passes the name filter; it is
  // shown when it matches or any descendant does.
  void updateNodeFilter(vsr::scene::Layer &layer);
  bool nodeShown(size_t nodeIndex) const;

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

  std::string m_nameFilter;
  bool m_showOnlySurfaces{false};
  bool m_showOnlyVolumes{false};
  bool m_showOnlyLights{false};
  // Set when the filter changes, so the next tree build expands every shown
  // node and the matches are visible without hunting for them.
  bool m_expandShownNodes{false};
  std::vector<bool> m_nodeShown;

  // The first selected node as of the end of the last build; a difference at
  // the start of a build means the selection changed outside this widget.
  vsr::scene::LayerNodeRef m_lastSeenSelection;
  // Node to bring into view on this build, and the indices of its ancestors
  // to force open.
  vsr::scene::LayerNodeRef m_revealNode;
  std::vector<size_t> m_revealAncestors;
};

} // namespace vsr::ui::imgui
