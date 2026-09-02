// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CameraRigEditor.h"
#include "ReplicaView.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <cmath>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

constexpr const char *REMOVE_POPUP = "Delete Camera Rig?";

const char *interpolationLabel(CameraInterpolation interpolation)
{
  switch (interpolation) {
  case CameraInterpolation::Hold:
    return "Hold";
  case CameraInterpolation::Linear:
    return "Linear";
  case CameraInterpolation::EaseOut:
    return "Ease Out";
  case CameraInterpolation::EaseIn:
    return "Ease In";
  case CameraInterpolation::EaseOutIn:
    return "Ease Out + In";
  }
  return "Linear";
}

const char *upAxisLabel(int upAxis)
{
  constexpr const char *LABELS[] = {"+x", "+y", "+z", "-x", "-y", "-z"};
  return upAxis >= 0 && upAxis < 6 ? LABELS[upAxis] : "?";
}

void poseRows(const ManipulatorState &state)
{
  const auto &pose = state.orbit;
  ImGui::Text(
      "Look at: %.3f %.3f %.3f", pose.lookat.x, pose.lookat.y, pose.lookat.z);
  ImGui::Text("Azimuth / elevation / distance: %.3f %.3f %.3f",
      pose.azeldist.x,
      pose.azeldist.y,
      pose.azeldist.z);
  if (std::isfinite(pose.fixedDist))
    ImGui::Text("Fixed distance: %.3f", pose.fixedDist);
  else
    ImGui::Text("Fixed distance: off");
  ImGui::Text("Up: %s   Mode: %s",
      upAxisLabel(pose.upAxis),
      pose.mode == 0 ? "Orbit" : "Look");
}

} // namespace

CameraRigEditor::CameraRigEditor(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Camera Rig"), m_browse(app, context)
{}

CameraRigEditor::~CameraRigEditor() = default;

void CameraRigEditor::onProjectReplaced()
{
  m_nameField.markStale();
}

// Selection //////////////////////////////////////////////////////////////////

const CameraRig *CameraRigEditor::resolveSelection(const Project &project)
{
  if (!m_selectOnArrival.empty()
      && replica::findCameraRig(project, m_selectOnArrival)) {
    m_selected = m_selectOnArrival;
    m_selectOnArrival.clear();
    m_selectedKeyframe = -1;
  }
  if (project.cameraRigs.empty()) {
    m_selected.clear();
    return nullptr;
  }
  const CameraRig *rig = replica::findCameraRig(project, m_selected);
  if (!rig) {
    rig = &project.cameraRigs.front();
    m_selected = rig->id;
    m_selectedKeyframe = -1;
  }
  return rig;
}

void CameraRigEditor::syncSelectionToActiveShot(const Project &project)
{
  const Shot *shot = replica::activeShot(project);
  const auto activeShotId = shot ? shot->id : ShotID{};
  if (activeShotId == m_lastActiveShotId)
    return;
  m_lastActiveShotId = activeShotId;
  if (shot && replica::findCameraRig(project, shot->cameraRigId)) {
    m_selected = shot->cameraRigId;
    m_selectedKeyframe = -1;
  }
}

// UI /////////////////////////////////////////////////////////////////////////

void CameraRigEditor::buildEditorUI(const Project &project)
{
  syncSelectionToActiveShot(project);
  buildUI_toolbar(project);

  const CameraRig *rig = resolveSelection(project);
  if (!rig) {
    ImGui::TextDisabled("No camera rigs");
    return;
  }

  if (ImGui::BeginCombo("Rig", rig->name.c_str())) {
    for (const auto &candidate : project.cameraRigs) {
      const bool selected = candidate.id == m_selected;
      ImGui::PushID(candidate.id.c_str());
      if (ImGui::Selectable(candidate.name.c_str(), selected)) {
        m_selected = candidate.id;
        m_selectedKeyframe = -1;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  rig = replica::findCameraRig(project, m_selected);
  if (!rig)
    return;

  buildUI_nameField(*rig);
  buildUI_rigActions(project, *rig);
  buildUI_keyframes(*rig);
}

void CameraRigEditor::buildUI_toolbar(const Project &project)
{
  auto createdReply = [this](const ProjectOpReply &reply,
                          const std::optional<CameraRigCreatedResult> &result) {
    if (!reply.ok)
      reportError(reply.error);
    else if (result)
      m_selectOnArrival = result->cameraRigId;
  };

  ImGui::BeginDisabled(pending(m_pendingOp));
  if (ImGui::Button("Add Rig"))
    m_pendingOp = ops().createCameraRig("", createdReply);

  ImGui::SameLine();
  ImGui::BeginDisabled(true);
  ImGui::Button("Clone Rig");
  ImGui::EndDisabled();
  vsr::ui::tooltipForPreviousItem(
      "Cloning a camera rig is not in the protocol yet");

  ImGui::SameLine();
  if (ImGui::Button("Load Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenFile;
    request.title = "Load Camera Rig Archive";
    request.extensions = ui::archiveExtensions();
    request.startDirectory = project.projectDirectory;
    request.onAccept = [this, createdReply](
                           const std::vector<std::filesystem::path> &paths) {
      m_pendingOp = ops().loadCameraRigArchive(paths.front(), createdReply);
    };
    m_browse.open(std::move(request));
  }
  ImGui::EndDisabled();
}

void CameraRigEditor::buildUI_nameField(const CameraRig &rig)
{
  const auto newName = m_nameField.draw(
      rig.id, rig.name, pending(m_pendingRename), "Invalid name: ");
  if (!newName)
    return;
  const CameraRigID id = rig.id;
  m_pendingRename = ops().renameCameraRig(
      id, *newName, [this, id](const ProjectOpReply &reply) {
        m_nameField.onReply(id, reply.ok, reply.error);
      });
}

void CameraRigEditor::buildUI_rigActions(
    const Project &project, const CameraRig &rig)
{
  const Shot *shot = replica::activeShot(project);
  const bool activeShotUsesRig = shot && shot->cameraRigId == rig.id;

  ImGui::BeginDisabled(pending(m_pendingOp));

  ImGui::BeginDisabled(!shot || activeShotUsesRig);
  if (ImGui::Button("Use for Active Shot") && shot) {
    Shot updated = *shot;
    updated.cameraRigId = rig.id;
    m_pendingOp = ops().updateShot(updated, errorReporter());
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Save Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::SaveFile;
    request.title = "Save Camera Rig Archive";
    request.extensions = ui::archiveExtensions();
    request.startDirectory = project.projectDirectory;
    request.defaultName = (rig.name.empty() ? rig.id : rig.name) + ".vsr";
    const CameraRigID id = rig.id;
    request.onAccept = [this, id](
                           const std::vector<std::filesystem::path> &paths) {
      m_pendingOp = ops().saveCameraRigArchive(
          id, ui::withVsrExtension(paths.front()), errorReporter());
    };
    m_browse.open(std::move(request));
  }

  ImGui::SameLine();
  if (ImGui::Button("Remove Rig")) {
    if (replica::cameraRigUseCount(project, rig.id) > 0) {
      m_rigToRemove = rig.id;
      ImGui::OpenPopup(REMOVE_POPUP);
    } else {
      m_pendingOp = ops().removeCameraRig(rig.id, errorReporter());
    }
  }

  ImGui::EndDisabled();
}

// Read-only in this milestone: no protocol op edits keyframes or the pose.
void CameraRigEditor::buildUI_keyframes(const CameraRig &rig)
{
  ImGui::SeparatorText("Current View");
  poseRows(rig.current);

  ImGui::SeparatorText("Keyframes");
  ImGui::TextDisabled(
      "Keyframe editing (Set View, Capture, Update, Delete) is not in the"
      " protocol yet; shown read-only.");

  if (m_selectedKeyframe >= int(rig.keyframes.size()))
    m_selectedKeyframe = rig.keyframes.empty() ? -1 : 0;

  if (rig.keyframes.empty()) {
    ImGui::TextDisabled("No keyframes");
    return;
  }

  if (ImGui::BeginTable(
          "keyframes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn(
        "", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableSetupColumn("Frame");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Interpolation");
    ImGui::TableSetupColumn("Pose (az el dist)");
    ImGui::TableHeadersRow();

    for (int i = 0; i < int(rig.keyframes.size()); ++i) {
      const auto &keyframe = rig.keyframes[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      if (m_selectedKeyframe == i) {
        ImGui::TableSetBgColor(
            ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
      }
      ImGui::TableNextColumn();
      if (ImGui::RadioButton("##selected", m_selectedKeyframe == i))
        m_selectedKeyframe = i;
      ImGui::TableNextColumn();
      ImGui::Text("%d", keyframe.frame);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(keyframe.name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(interpolationLabel(keyframe.interpolationToNext));
      ImGui::TableNextColumn();
      const auto &pose = keyframe.manipulator.orbit;
      ImGui::Text(
          "%.2f %.2f %.2f", pose.azeldist.x, pose.azeldist.y, pose.azeldist.z);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (m_selectedKeyframe >= 0
      && m_selectedKeyframe < int(rig.keyframes.size())) {
    const auto &keyframe = rig.keyframes[m_selectedKeyframe];
    const auto label = keyframe.name.empty()
        ? ("Keyframe pose: frame " + std::to_string(keyframe.frame))
        : ("Keyframe pose: " + keyframe.name);
    if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      poseRows(keyframe.manipulator);
  }
}

// Popups /////////////////////////////////////////////////////////////////////

void CameraRigEditor::buildPopups(const Project &project)
{
  buildUI_removeConfirmation(project);
  m_browse.renderUI();
}

void CameraRigEditor::buildUI_removeConfirmation(const Project &project)
{
  const CameraRig *rig = replica::findCameraRig(project, m_rigToRemove);
  const size_t useCount = replica::cameraRigUseCount(project, m_rigToRemove);
  const auto choice = ui::confirmModal(REMOVE_POPUP,
      "Delete '" + (rig ? rig->name : m_rigToRemove) + "' and clear "
          + std::to_string(useCount) + " shot reference"
          + (useCount == 1 ? "" : "s") + "?",
      "Delete",
      canSend());
  if (choice == ui::ConfirmChoice::Confirmed)
    m_pendingOp = ops().removeCameraRig(m_rigToRemove, errorReporter());
  if (choice != ui::ConfirmChoice::Pending)
    m_rigToRemove.clear();
}

} // namespace vsr::scivis_studio::client
