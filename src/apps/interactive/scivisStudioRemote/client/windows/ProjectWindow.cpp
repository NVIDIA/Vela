// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectWindow.h"
// scivisStudioClient
#include "Application.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// imgui
#include <imgui.h>

namespace vsr::scivis_studio::client {

namespace {

constexpr const char *REMOVE_SHOT_POPUP = "Remove Shot?";

} // namespace

ProjectWindow::ProjectWindow(Application *app, EditorContext *context)
    : EditorWindow(app, context, "Project"), m_studio(app)
{}

ProjectWindow::~ProjectWindow() = default;

void ProjectWindow::buildEditorUI(const Project &project)
{
  if (ImGui::Button("New"))
    m_studio->newProject();
  ImGui::SameLine();
  if (ImGui::Button("Open..."))
    m_studio->openProjectDialog();
  ImGui::SameLine();
  if (ImGui::Button("Save"))
    m_studio->saveProject();
  ImGui::SameLine();
  if (ImGui::Button("Save As..."))
    m_studio->saveProjectAsDialog();

  ImGui::Text("Name: %s", project.name.c_str());
  ImGui::TextWrapped(
      "Path: %s", project::projectDirectoryText(project).c_str());
  ImGui::Text("Status: %s", project.dirty ? "dirty" : "clean");

  ImGui::SeparatorText("Datasets");
  if (project.datasets.empty())
    ImGui::TextDisabled("No datasets");
  for (const auto &dataset : project.datasets) {
    const bool unloaded = dataset.residency == DatasetResidency::Unloaded;
    if (unloaded) {
      ImGui::PushStyleColor(
          ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    ImGui::BulletText(
        "%s  [%s]", dataset.name.c_str(), dataset::displayStatus(dataset));
    if (unloaded)
      ImGui::PopStyleColor();
  }

  buildUI_shots(project);
}

void ProjectWindow::buildUI_shots(const Project &project)
{
  ImGui::SeparatorText("Shots");
  ProjectOps &ops = m_context->ops();

  ImGui::BeginDisabled(m_create.busy(ops));
  if (ImGui::Button("Add Shot")) {
    // An empty name lets the server number the shot.
    m_create.sendForResult<protocol::ShotCreatedResult>(ops,
        protocol::CreateShot{},
        [this](const protocol::ProjectOpReply &reply,
            const std::optional<protocol::ShotCreatedResult> &) {
          if (!reply.ok)
            m_context->error(reply.error);
        });
  }
  ImGui::EndDisabled();

  if (project.shots.empty())
    ImGui::TextDisabled("No shots");

  const bool busy = m_setActive.busy(ops) || m_remove.busy(ops);
  // The popup is opened once the per-shot id is off the stack, since
  // buildPopups() begins it at window scope and ImGui hashes popup ids with
  // whatever is pushed at the time.
  bool openRemove = false;
  ImGui::BeginDisabled(busy);
  for (const auto &shot : project.shots) {
    ImGui::PushID(shot.id.c_str());
    const bool active = shot.id == project.activeShotId;
    if (ImGui::Selectable(shot.name.c_str(),
            active,
            ImGuiSelectableFlags_AllowOverlap,
            ImVec2(ImGui::GetContentRegionAvail().x - 70.f, 0.f))
        && !active) {
      protocol::SetActiveShot activate;
      activate.shotId = shot.id;
      m_setActive.send(ops, std::move(activate), m_context->errorReporter());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      m_shotToRemove = shot.id;
      openRemove = true;
    }
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  if (openRemove)
    ImGui::OpenPopup(REMOVE_SHOT_POPUP);
}

void ProjectWindow::buildPopups(const Project &project)
{
  const Shot *shot = project::findShot(project, m_shotToRemove);
  const auto choice = ui::confirmModal(REMOVE_SHOT_POPUP,
      "Remove shot '" + (shot ? shot->name : m_shotToRemove) + "'?",
      "Remove",
      m_context->canSend(),
      [&] {
        if (project.shots.size() <= 1)
          ui::warningText("This is the project's only shot.");
      });
  if (choice == ui::ConfirmChoice::Confirmed) {
    protocol::RemoveShot remove;
    remove.shotId = m_shotToRemove;
    m_remove.send(
        m_context->ops(), std::move(remove), m_context->errorReporter());
  }
  if (choice != ui::ConfirmChoice::Pending)
    m_shotToRemove.clear();
}

} // namespace vsr::scivis_studio::client
