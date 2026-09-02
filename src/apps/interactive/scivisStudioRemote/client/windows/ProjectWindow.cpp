// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectWindow.h"
#include "ReplicaView.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// imgui
#include <imgui.h>

namespace vsr::scivis_studio::client {

namespace {

constexpr const char *REMOVE_SHOT_POPUP = "Remove Shot?";

} // namespace

ProjectWindow::ProjectWindow(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Project")
{}

ProjectWindow::~ProjectWindow() = default;

void ProjectWindow::buildEditorUI(const Project &project)
{
  const auto &actions = m_context->actions;
  if (ImGui::Button("New") && actions.newProject)
    actions.newProject();
  ImGui::SameLine();
  if (ImGui::Button("Open...") && actions.openProject)
    actions.openProject();
  ImGui::SameLine();
  if (ImGui::Button("Save") && actions.saveProject)
    actions.saveProject();
  ImGui::SameLine();
  if (ImGui::Button("Save As...") && actions.saveProjectAs)
    actions.saveProjectAs();

  ImGui::Text("Name: %s", project.name.c_str());
  ImGui::TextWrapped(
      "Path: %s", replica::projectDirectoryText(project).c_str());
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
    ImGui::BulletText("%s  [%s]",
        dataset.name.c_str(),
        replica::datasetStatusText(dataset));
    if (unloaded)
      ImGui::PopStyleColor();
  }

  buildUI_shots(project);
}

void ProjectWindow::buildUI_shots(const Project &project)
{
  ImGui::SeparatorText("Shots");

  ImGui::BeginDisabled(pending(m_pendingCreate));
  if (ImGui::Button("Add Shot")) {
    const auto name = "Shot " + std::to_string(project.shots.size() + 1);
    m_pendingCreate = ops().createShot(name,
        [this](const protocol::ProjectOpReply &reply,
            const std::optional<protocol::ShotCreatedResult> &) {
          if (!reply.ok)
            reportError(reply.error);
        });
  }
  ImGui::EndDisabled();

  if (project.shots.empty())
    ImGui::TextDisabled("No shots");

  const bool busy = pending(m_pendingSetActive) || pending(m_pendingRemove);
  ImGui::BeginDisabled(busy);
  for (const auto &shot : project.shots) {
    ImGui::PushID(shot.id.c_str());
    const bool active = shot.id == project.activeShotId;
    if (ImGui::Selectable(shot.name.c_str(),
            active,
            ImGuiSelectableFlags_AllowOverlap,
            ImVec2(ImGui::GetContentRegionAvail().x - 70.f, 0.f))
        && !active) {
      m_pendingSetActive = ops().setActiveShot(shot.id, errorReporter());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      m_shotToRemove = shot.id;
      ImGui::OpenPopup(REMOVE_SHOT_POPUP);
    }
    ImGui::PopID();
  }
  ImGui::EndDisabled();
}

void ProjectWindow::buildPopups(const Project &project)
{
  if (!ImGui::BeginPopupModal(
          REMOVE_SHOT_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  const Shot *shot = replica::findShot(project, m_shotToRemove);
  ImGui::Text("Remove shot '%s'?",
      shot ? shot->name.c_str() : m_shotToRemove.c_str());
  if (project.shots.size() <= 1)
    ui::warningText("This is the project's only shot.");

  ImGui::BeginDisabled(!canSend());
  if (ImGui::Button("Remove")) {
    m_pendingRemove = ops().removeShot(m_shotToRemove, errorReporter());
    m_shotToRemove.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_shotToRemove.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace vsr::scivis_studio::client
