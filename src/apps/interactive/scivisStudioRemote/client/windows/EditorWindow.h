// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client
#include "EditorContext.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/Window.h"

namespace vsr::scivis_studio {
struct Project;
}

namespace vsr::scivis_studio::client {

/*
 * Base of the client's copies of the Studio editors. It reads the Project
 * Replica and sends Project Ops; it never mutates the replica (nothing is
 * applied optimistically) and never touches ProjectContext or files. The
 * body is disabled whenever the connection cannot take requests (Lost,
 * bootstrapping), and shows a hint instead of a project while there is none.
 *
 * Subclasses draw the editor in buildEditorUI() and their popups (confirm
 * dialogs, the Remote Browse dialog) in buildPopups(), which runs outside
 * the disabled scope so a popup can still be dismissed while Lost. Every
 * snapshot replaces the replica wholesale, so subclasses keep ids, not
 * indices or pointers, and re-resolve them in onProjectReplaced().
 *
 * Example:
 *   struct ShotList : EditorWindow {
 *     ShotList(Application *app, EditorContext *c) : EditorWindow(app, c, "Shots") {}
 *     void buildEditorUI(const Project &project) override { ... }
 *   };
 */
struct EditorWindow : public vsr::ui::imgui::Window
{
  EditorWindow(vsr::ui::imgui::Application *app,
      EditorContext *context,
      const char *name);
  ~EditorWindow() override;

  void buildUI() final;

  // A Project Snapshot replaced the replica; pointers into the old one are
  // dead and text buffers should re-read their values.
  virtual void onProjectReplaced();

 protected:
  virtual void buildEditorUI(const Project &project) = 0;
  virtual void buildPopups(const Project &project);

  const Project *project() const;
  ProjectOps &ops() const;
  bool canSend() const;
  // True while `handle` awaits its reply: grey the control that sent it.
  bool pending(RequestHandle handle) const;
  void reportError(const std::string &message) const;
  ReplyCallback errorReporter() const;

  EditorContext *m_context{nullptr};
};

} // namespace vsr::scivis_studio::client
