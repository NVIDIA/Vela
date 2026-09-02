// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "EditorWindow.h"
// vsr_scivis_studio_model
#include "Shot.h"
// std
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * The client's copy of the Shot Editor: edits the active shot's fields and
 * sends each committed edit as one UpdateShot carrying the whole Shot. The
 * controls work on a draft copied from the replica; text and number fields
 * commit when they deactivate (so a slider or a typed value travels once),
 * checkboxes and combos commit on change. While an update is pending the
 * editor is greyed; the reply (and the snapshot behind it) then refreshes
 * the draft, so a rejected edit visibly snaps back. Renderer libraries and
 * renderers come from the Structural Mirror's Renderer objects, which is all
 * the client can know about the server's devices.
 *
 * Not here: transport (Play/Stop, current frame) is milestone 6 and Render
 * Active Shot is milestone 7; `playing` is never sent.
 */
struct ShotEditor : public EditorWindow
{
  ShotEditor(vsr::ui::imgui::Application *app, EditorContext *context);
  ~ShotEditor() override;

  void onProjectReplaced() override;

 private:
  void buildEditorUI(const Project &project) override;

  void syncDraft(const Project &project);
  void sendDraft();
  void buildUI_deviceSelector();
  void buildUI_rendererSelector();
  void buildUI_lightRigSelector(const Project &project);
  void buildUI_cameraRigSelector(const Project &project);
  void buildUI_datasets(const Project &project);

  std::optional<Shot> m_draft;
  // The replica changed (or an edit was rejected) since the draft was taken.
  bool m_draftStale{true};
  RequestHandle m_pendingUpdate;
};

} // namespace vsr::scivis_studio::client
