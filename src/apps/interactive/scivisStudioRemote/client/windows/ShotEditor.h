// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
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
 * the draft, so a rejected edit visibly snaps back. Because UpdateShot is a
 * whole-Shot replace, the draft is rebased on the replica's current shot as
 * it is sent: only the fields edited since the draft was taken travel, so an
 * edit another editor made meanwhile (a rig chosen in its own editor) is not
 * silently reverted. Renderer libraries and renderers come from the
 * Structural Mirror's Renderer objects, which is all the client can know
 * about the server's devices.
 *
 * "Render Shot..." confirms the frame count and output directory, then
 * sends RenderShot: the server makes the shot active and renders it as a
 * Server Task the Tasks panel follows (and can cancel); it is refused
 * unless the project is saved and no render is queued or running.
 *
 * Not here: transport (Play/Stop, current frame, loop, fps) lives in the
 * Timeline window; `playing` is never sent from here.
 */
struct ShotEditor : public EditorWindow
{
  ShotEditor(vsr::ui::imgui::Application *app, EditorContext *context);
  ~ShotEditor() override;

  void onProjectReplaced() override;

 private:
  void buildEditorUI(const Project &project) override;
  void buildPopups(const Project &project) override;

  void syncDraft(const Project &project);
  void buildUI_render(const Project &project, const Shot &shot);
  void sendDraft();
  void buildUI_deviceSelector();
  void buildUI_rendererSelector();
  void buildUI_lightRigSelector(const Project &project);
  void buildUI_cameraRigSelector(const Project &project);
  void buildUI_datasets(const Project &project);

  std::optional<Shot> m_draft;
  // The replica's shot the draft was copied from: what the draft is compared
  // against to tell the user's edits from the rest when it is sent.
  Shot m_draftBase;
  // The replica changed (or an edit was rejected) since the draft was taken.
  bool m_draftStale{true};
  RequestHandle m_pendingUpdate;
  // The shot the Render confirmation is open for; empty when it is not.
  ShotID m_shotToRender;
  RequestHandle m_pendingRender;
};

} // namespace vsr::scivis_studio::client
