// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectLocationDialog.h"
// imgui
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace vsr::scivis_studio::modals {

ProjectLocationDialog::ProjectLocationDialog(vsr::ui::imgui::Application *app,
    std::unique_ptr<BrowseProvider> browse,
    std::unique_ptr<Action> action)
    : Modal(app, "Project Location"),
      m_browse(std::move(browse)),
      m_action(std::move(action))
{}

ProjectLocationDialog::~ProjectLocationDialog() = default;

void ProjectLocationDialog::reset()
{
  m_error.clear();
  m_action->reset();
}

void ProjectLocationDialog::configure(ProjectLocationMode mode)
{
  m_mode = mode;
  reset();
  auto initial = m_action->initialDirectory(mode);
  if (!initial.empty())
    m_directory = std::move(initial);
}

void ProjectLocationDialog::submit()
{
  if (m_directory.empty()) {
    m_error = "Enter a project directory.";
    return;
  }

  Request request;
  request.mode = m_mode;
  request.directory = m_directory;

  m_error.clear();
  m_action->submit(request, [this](bool ok, const std::string &error) {
    if (!ok) {
      m_error = error;
      return;
    }
    hide();
  });
}

void ProjectLocationDialog::buildUI()
{
  const bool open = m_mode == ProjectLocationMode::OpenProject;
  ImGui::TextUnformatted(open ? "Open Project" : "Save Project As");

  const bool busy = m_action->busy();
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
    m_browse->browse(std::move(request));
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(520.f);
  const bool entered = ImGui::InputText(
      "Directory", &m_directory, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::EndDisabled();

  switch (modalFooter(
      *m_browse, *m_action, m_error, open ? "Open" : "Save", entered)) {
  case ModalChoice::Cancelled:
    reset();
    hide();
    break;
  case ModalChoice::Submitted:
    submit();
    break;
  case ModalChoice::None:
    break;
  }
}

} // namespace vsr::scivis_studio::modals
