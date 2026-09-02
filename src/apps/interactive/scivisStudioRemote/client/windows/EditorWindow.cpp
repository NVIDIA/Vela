// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "EditorWindow.h"
// scivisStudioClient
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// imgui
#include <imgui.h>

namespace vsr::scivis_studio::client {

EditorWindow::EditorWindow(
    vsr::ui::imgui::Application *app, EditorContext *context, const char *name)
    : Window(app, name), m_context(context)
{}

EditorWindow::~EditorWindow() = default;

void EditorWindow::buildUI()
{
  const Project *project = m_context ? m_context->project() : nullptr;
  if (!project) {
    ImGui::TextDisabled("Not connected");
    return;
  }

  if (m_context->renderInProgress())
    ui::warningText(
        "Render in progress: the server refuses edits until it ends");

  ImGui::BeginDisabled(!canSend());
  buildEditorUI(*project);
  ImGui::EndDisabled();

  buildPopups(*project);
}

void EditorWindow::onProjectReplaced() {}

void EditorWindow::buildPopups(const Project &) {}

const Project *EditorWindow::project() const
{
  return m_context->project();
}

ProjectOps &EditorWindow::ops() const
{
  return m_context->ops();
}

bool EditorWindow::canSend() const
{
  return m_context->canSend();
}

bool EditorWindow::pending(RequestHandle handle) const
{
  return handle.valid() && ops().pending(handle);
}

void EditorWindow::reportError(const std::string &message) const
{
  m_context->error(message);
}

ReplyCallback EditorWindow::errorReporter() const
{
  return m_context->errorReporter();
}

} // namespace vsr::scivis_studio::client
