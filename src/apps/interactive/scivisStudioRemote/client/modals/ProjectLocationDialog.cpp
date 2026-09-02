// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectLocationDialog.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>

namespace vsr::scivis_studio::client {

ProjectLocationDialog::ProjectLocationDialog(vsr::ui::imgui::Application *app,
    EditorContext *context,
    UIStateProvider uiState)
    : Modal(app, "Project Location"),
      m_context(context),
      m_uiState(std::move(uiState)),
      m_browse(app, context)
{}

ProjectLocationDialog::~ProjectLocationDialog() = default;

void ProjectLocationDialog::configure(ProjectLocationMode mode)
{
  m_mode = mode;
  m_error.clear();
  m_pending = {};
  if (const Project *project = m_context->project()) {
    if (m_mode == ProjectLocationMode::SaveProjectAs
        && !project->projectDirectory.empty())
      m_directory = project->projectDirectory.generic_string();
  }
}

void ProjectLocationDialog::submit()
{
  if (m_directory.empty()) {
    m_error = "Enter a project directory.";
    return;
  }
  const std::filesystem::path directory(m_directory);
  auto onReply = [this](const protocol::ProjectOpReply &reply,
                     const std::optional<protocol::TaskStartedResult> &) {
    if (reply.requestId != m_pending.requestId)
      return;
    m_pending = {};
    if (!reply.ok) {
      m_error = reply.error;
      return;
    }
    hide();
  };
  m_error.clear();
  if (m_mode == ProjectLocationMode::OpenProject)
    m_pending = m_context->ops().openProject(directory, onReply);
  else
    m_pending = m_context->ops().saveProject(
        directory, m_uiState ? m_uiState() : nullptr, onReply);
}

void ProjectLocationDialog::buildUI()
{
  const bool open = m_mode == ProjectLocationMode::OpenProject;
  ImGui::TextUnformatted(open ? "Open Project" : "Save Project As");

  const bool busy = m_pending.valid() && m_context->ops().pending(m_pending);
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Browse...")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenDirectory;
    request.title = open ? "Choose the project directory to open"
                         : "Choose the directory to save the project into";
    request.startDirectory = m_directory;
    request.onAccept = [this](const std::vector<std::filesystem::path> &paths) {
      if (!paths.empty())
        m_directory = paths.front().generic_string();
    };
    m_browse.open(std::move(request));
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(520.f);
  const bool entered = ImGui::InputText(
      "Directory", &m_directory, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::EndDisabled();

  if (busy)
    ImGui::TextDisabled("waiting for the server...");
  ui::errorText(m_error);

  m_browse.renderUI();

  ImGui::Spacing();
  if (ImGui::Button("Cancel")
      || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_browse.visible())) {
    hide();
    return;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(busy || !m_context->canSend());
  if (ImGui::Button(open ? "Open" : "Save") || (entered && !busy))
    submit();
  ImGui::EndDisabled();
}

} // namespace vsr::scivis_studio::client
