// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorWindow.h"
// vsr_scivis_studio_model
#include "Shot.h"
// std
#include <string>

namespace vsr::scivis_studio::client {

/*
 * The client's copy of the Shot Editor: edits the active shot's fields and
 * sends each committed edit as one UpdateShot carrying a ShotPatch of that
 * field alone (the renderer pick is one edit across its three fields). The
 * controls show the replica's shot as it is each UI frame; text and number
 * fields commit when they deactivate (so a slider or a typed value travels
 * once), checkboxes and combos commit on change. While an update is pending
 * the editor is greyed; the reply's snapshot then shows what the server kept,
 * so a rejected edit visibly snaps back. There is no draft: a field another
 * window edits meanwhile (the Timeline's fps, a rig editor's "Use for Active
 * Shot") is never in a patch from here, so it cannot be reverted. Renderer
 * libraries and renderers come from the Structural Mirror's Renderer
 * objects, which is all the client can know about the server's devices.
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

 private:
  void buildEditorUI(const Project &project) override;
  void buildPopups(const Project &project) override;

  // Sends the patch for `shot`; the reply reports a refusal.
  void commit(const Shot &shot, const ShotPatch &patch);
  void buildUI_playback(const Shot &shot);
  void buildUI_renderSettings(const Shot &shot);
  void buildUI_deviceSelector(const Shot &shot);
  void buildUI_rendererSelector(const Shot &shot);
  void buildUI_lightRigSelector(const Project &project, const Shot &shot);
  void buildUI_cameraRigSelector(const Project &project, const Shot &shot);
  void buildUI_render(const Project &project, const Shot &shot);
  void buildUI_datasets(const Project &project, const Shot &shot);

  RequestHandle m_pendingUpdate;
  // The shot the Render confirmation is open for; empty when it is not.
  ShotID m_shotToRender;
  RequestHandle m_pendingRender;
};

} // namespace vsr::scivis_studio::client
