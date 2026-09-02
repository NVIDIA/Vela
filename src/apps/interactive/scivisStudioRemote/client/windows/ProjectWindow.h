// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorWindow.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// std
#include <string>

namespace vsr::scivis_studio::client {

/*
 * The client's copy of the monolith's Project window: project name, server
 * directory and dirty state, the dataset inventory with status and residency,
 * and the shot list. Clicking a shot sends SetActiveShot; Add and Remove send
 * CreateShot/RemoveShot; New/Open/Save go through the application's project
 * actions (and their Remote Browse dialog). The highlighted shot is the
 * replica's activeShotId, never the click.
 */
struct ProjectWindow : public EditorWindow
{
  ProjectWindow(vsr::ui::imgui::Application *app, EditorContext *context);
  ~ProjectWindow() override;

 private:
  void buildEditorUI(const Project &project) override;
  void buildPopups(const Project &project) override;

  void buildUI_shots(const Project &project);

  RequestHandle m_pendingSetActive;
  RequestHandle m_pendingCreate;
  RequestHandle m_pendingRemove;
  ShotID m_shotToRemove;
};

} // namespace vsr::scivis_studio::client
