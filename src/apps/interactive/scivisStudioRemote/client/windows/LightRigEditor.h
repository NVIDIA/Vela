// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorWindow.h"
#include "RemoteBrowseDialog.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "LightRig.h"
// vsr_scene
#include "vsr/scene/Layer.hpp"
// std
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * The client's copy of the Light Rig editor: create, clone, rename, remove
 * and archive rigs, add a light by ANARI subtype and remove one, all as
 * Project Ops; the lights themselves are the Structural Mirror's nodes under
 * the rig's root (`LightRig::rootNode` from the replica), and selecting one
 * hands it to the Object Editor, whose parameter edits flow out through the
 * mirror delegate. The rig selection is an id, re-resolved per snapshot and
 * following the active shot's rig as in the monolith.
 *
 * Renaming a light has no protocol op this milestone and is read-only here
 * (README, open design questions).
 */
struct LightRigEditor : public EditorWindow
{
  LightRigEditor(vsr::ui::imgui::Application *app, EditorContext *context);
  ~LightRigEditor() override;

  void onProjectReplaced() override;

 private:
  void buildEditorUI(const Project &project) override;
  void buildPopups(const Project &project) override;

  const LightRig *resolveSelection(const Project &project);
  void syncSelectionToActiveShot(const Project &project);
  void buildUI_toolbar(const Project &project);
  void buildUI_nameField(const LightRig &rig);
  void buildUI_rigActions(const Project &project, const LightRig &rig);
  void buildUI_lightList(const LightRig &rig);
  void buildUI_removeConfirmation(const Project &project);
  std::vector<vsr::scene::LayerNodeRef> lightNodes(const LightRig &rig) const;

  LightRigID m_selected;
  ShotID m_lastActiveShotId;
  LightRigID m_lastActiveShotLightRigId;
  // A create/clone/load reply names the rig to select once it appears.
  LightRigID m_selectOnArrival;

  RequestHandle m_pendingOp;
  RequestHandle m_pendingRename;
  RequestHandle m_pendingLightOp;

  ui::BufferedNameField m_nameField;

  LightRigID m_rigToRemove;
  RemoteBrowseDialog m_browse;
};

} // namespace vsr::scivis_studio::client
