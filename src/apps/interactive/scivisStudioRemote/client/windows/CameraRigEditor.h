// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorWindow.h"
#include "RemoteBrowseDialog.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "CameraRig.h"
// std
#include <string>

namespace vsr::scivis_studio::client {

/*
 * The client's copy of the Camera Rig editor: create, rename, remove and
 * archive rigs as Project Ops and make one the active shot's rig through
 * UpdateShot. The keyframe table and the current pose are shown read-only:
 * cloning a rig and editing keyframes (Set View, Capture, Update, Delete,
 * the pose editor) have no protocol op this milestone (README, open design
 * questions). The selection is an id, re-resolved per snapshot and following
 * the active shot's rig.
 */
struct CameraRigEditor : public EditorWindow
{
  CameraRigEditor(vsr::ui::imgui::Application *app, EditorContext *context);
  ~CameraRigEditor() override;

  void onProjectReplaced() override;

 private:
  void buildEditorUI(const Project &project) override;
  void buildPopups(const Project &project) override;

  const CameraRig *resolveSelection(const Project &project);
  void syncSelectionToActiveShot(const Project &project);
  void buildUI_toolbar(const Project &project);
  void buildUI_nameField(const CameraRig &rig);
  void buildUI_rigActions(const Project &project, const CameraRig &rig);
  void buildUI_keyframes(const CameraRig &rig);
  void buildUI_removeConfirmation(const Project &project);

  CameraRigID m_selected;
  ShotID m_lastActiveShotId;
  CameraRigID m_selectOnArrival;
  int m_selectedKeyframe{-1};

  RequestHandle m_pendingOp;
  RequestHandle m_pendingRename;

  ui::BufferedNameField m_nameField;

  CameraRigID m_rigToRemove;
  RemoteBrowseDialog m_browse;
};

} // namespace vsr::scivis_studio::client
