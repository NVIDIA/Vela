// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LightRigEditor.h"
#include "ReplicaView.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <array>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

constexpr const char *REMOVE_POPUP = "Delete Light Rig?";
constexpr const char *ADD_LIGHT_POPUP = "Add Light";
const std::vector<std::string> ARCHIVE_EXTENSIONS = {".vsr", ".tsd"};

struct LightTypeOption
{
  const char *label;
  const char *subtype;
};

constexpr std::array<LightTypeOption, 5> LIGHT_TYPES = {
    {{"Directional", "directional"},
        {"Point", "point"},
        {"Quad", "quad"},
        {"Spot", "spot"},
        {"Ring", "ring"}}};

std::string lightName(vsr::scene::LayerNodeRef node)
{
  if (!node)
    return "<missing>";
  auto *object = (*node)->getObject();
  std::string label = (*node)->name();
  if (label.empty() && object)
    label = object->name();
  if (label.empty())
    label = "Light";
  return label;
}

std::string lightSubtype(vsr::scene::LayerNodeRef node)
{
  if (!node)
    return "";
  auto *object = (*node)->getObject();
  return object ? object->subtype().str() : "";
}

} // namespace

LightRigEditor::LightRigEditor(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Light Rig"), m_browse(app, context)
{}

LightRigEditor::~LightRigEditor() = default;

void LightRigEditor::onProjectReplaced()
{
  m_nameStale = true;
}

// Selection //////////////////////////////////////////////////////////////////

const LightRig *LightRigEditor::resolveSelection(const Project &project)
{
  if (!m_selectOnArrival.empty()) {
    if (replica::findLightRig(project, m_selectOnArrival)) {
      m_selected = m_selectOnArrival;
      m_selectOnArrival.clear();
    }
  }
  if (project.lightRigs.empty()) {
    m_selected.clear();
    return nullptr;
  }
  const LightRig *rig = replica::findLightRig(project, m_selected);
  if (!rig) {
    rig = &project.lightRigs.front();
    m_selected = rig->id;
  }
  return rig;
}

void LightRigEditor::syncSelectionToActiveShot(const Project &project)
{
  const Shot *shot = replica::activeShot(project);
  const auto activeShotId = shot ? shot->id : ShotID{};
  const auto activeRigId = shot ? shot->lightRigId : LightRigID{};
  if (activeShotId == m_lastActiveShotId
      && activeRigId == m_lastActiveShotLightRigId)
    return;
  m_lastActiveShotId = activeShotId;
  m_lastActiveShotLightRigId = activeRigId;
  if (shot && replica::findLightRig(project, shot->lightRigId))
    m_selected = shot->lightRigId;
}

// The rig's lights are the ANARI_LIGHT object nodes below its root in the
// mirror's "studio" layer.
std::vector<vsr::scene::LayerNodeRef> LightRigEditor::lightNodes(
    const LightRig &rig) const
{
  std::vector<vsr::scene::LayerNodeRef> nodes;
  if (rig.rootNode.layerName.empty()
      || rig.rootNode.nodeIndex == VSR_INVALID_INDEX)
    return nodes;
  auto &scene = appContext()->vsr.scene;
  auto *layer = scene.layer(rig.rootNode.layerName);
  if (!layer || rig.rootNode.nodeIndex >= layer->capacity())
    return nodes;
  auto root = layer->at(rig.rootNode.nodeIndex);
  if (!root)
    return nodes;
  layer->traverse(root, [&](auto &node, int level) {
    if (level > 0 && node->isObject() && node->type() == ANARI_LIGHT)
      nodes.push_back(layer->at(node.index()));
    return true;
  });
  return nodes;
}

// UI /////////////////////////////////////////////////////////////////////////

void LightRigEditor::buildEditorUI(const Project &project)
{
  syncSelectionToActiveShot(project);
  buildUI_toolbar(project);

  const LightRig *rig = resolveSelection(project);
  if (!rig) {
    ImGui::TextDisabled("No light rigs");
    return;
  }

  if (ImGui::BeginCombo("Rig", rig->name.c_str())) {
    for (const auto &candidate : project.lightRigs) {
      const bool selected = candidate.id == m_selected;
      ImGui::PushID(candidate.id.c_str());
      if (ImGui::Selectable(candidate.name.c_str(), selected))
        m_selected = candidate.id;
      if (selected)
        ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  rig = replica::findLightRig(project, m_selected);
  if (!rig)
    return;

  buildUI_nameField(*rig);
  buildUI_rigActions(project, *rig);
  buildUI_lightList(*rig);
}

void LightRigEditor::buildUI_toolbar(const Project &project)
{
  auto createdReply = [this](const ProjectOpReply &reply,
                          const std::optional<LightRigCreatedResult> &result) {
    if (!reply.ok)
      reportError(reply.error);
    else if (result)
      m_selectOnArrival = result->lightRigId;
  };

  ImGui::BeginDisabled(pending(m_pendingOp));
  if (ImGui::Button("Add Rig"))
    m_pendingOp = ops().createLightRig("", createdReply);

  ImGui::SameLine();
  ImGui::BeginDisabled(m_selected.empty());
  if (ImGui::Button("Clone Rig"))
    m_pendingOp = ops().cloneLightRig(m_selected, createdReply);
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Load Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenFile;
    request.title = "Load Light Rig Archive";
    request.extensions = ARCHIVE_EXTENSIONS;
    request.startDirectory = project.projectDirectory;
    request.onAccept = [this, createdReply](
                           const std::vector<std::filesystem::path> &paths) {
      m_pendingOp = ops().loadLightRigArchive(paths.front(), createdReply);
    };
    m_browse.open(std::move(request));
  }
  ImGui::EndDisabled();
}

// Buffered, reject-on-commit: the buffer travels as RenameLightRig and a
// failed reply restores the replica's name.
void LightRigEditor::buildUI_nameField(const LightRig &rig)
{
  const bool refresh = m_nameStale && !ImGui::IsAnyItemActive()
      && !pending(m_pendingRename);
  if (m_nameBufferRig != rig.id || refresh) {
    if (m_nameBufferRig != rig.id)
      m_nameError.clear();
    m_nameBufferRig = rig.id;
    m_nameBuffer = rig.name;
    m_nameStale = false;
  }

  ImGui::BeginDisabled(pending(m_pendingRename));
  const bool entered = ImGui::InputText(
      "Name", &m_nameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
  const bool commit = entered || ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();

  if (commit && m_nameBuffer != rig.name) {
    const LightRigID id = rig.id;
    m_pendingRename = ops().renameLightRig(
        id, m_nameBuffer, [this, id](const ProjectOpReply &reply) {
          if (reply.ok) {
            m_nameError.clear();
          } else {
            m_nameError = reply.error;
            if (m_nameBufferRig == id)
              m_nameStale = true;
          }
        });
  } else if (commit) {
    m_nameError.clear();
  }
  if (!m_nameError.empty())
    ui::errorText("Invalid name: " + m_nameError);
}

void LightRigEditor::buildUI_rigActions(
    const Project &project, const LightRig &rig)
{
  const Shot *shot = replica::activeShot(project);
  const bool activeShotUsesRig = shot && shot->lightRigId == rig.id;

  ImGui::BeginDisabled(pending(m_pendingOp));

  ImGui::BeginDisabled(!shot || activeShotUsesRig);
  if (ImGui::Button("Use for Active Shot") && shot) {
    Shot updated = *shot;
    updated.lightRigId = rig.id;
    m_pendingOp = ops().updateShot(updated, errorReporter());
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Save Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::SaveFile;
    request.title = "Save Light Rig Archive";
    request.extensions = ARCHIVE_EXTENSIONS;
    request.startDirectory = project.projectDirectory;
    request.defaultName = (rig.name.empty() ? rig.id : rig.name) + ".vsr";
    const LightRigID id = rig.id;
    request.onAccept = [this, id](
                           const std::vector<std::filesystem::path> &paths) {
      m_pendingOp = ops().saveLightRigArchive(
          id, ui::withVsrExtension(paths.front()), errorReporter());
    };
    m_browse.open(std::move(request));
  }

  ImGui::SameLine();
  if (ImGui::Button("Remove Rig")) {
    if (replica::lightRigUseCount(project, rig.id) > 0) {
      m_rigToRemove = rig.id;
      ImGui::OpenPopup(REMOVE_POPUP);
    } else {
      m_pendingOp = ops().removeLightRig(rig.id, errorReporter());
    }
  }

  ImGui::EndDisabled();
}

void LightRigEditor::buildUI_lightList(const LightRig &rig)
{
  auto *ctx = appContext();
  const auto nodes = lightNodes(rig);
  const auto firstSelected = ctx->getFirstSelected();
  int selectedLight = -1;
  if (firstSelected) {
    auto it = std::find(nodes.begin(), nodes.end(), firstSelected);
    if (it != nodes.end())
      selectedLight = int(std::distance(nodes.begin(), it));
  }

  ImGui::SeparatorText("Lights");

  ImGui::BeginDisabled(pending(m_pendingLightOp));
  if (ImGui::Button("Add Light"))
    ImGui::OpenPopup(ADD_LIGHT_POPUP);
  if (ImGui::BeginPopup(ADD_LIGHT_POPUP)) {
    for (const auto &type : LIGHT_TYPES) {
      if (ImGui::MenuItem(type.label)) {
        m_pendingLightOp = ops().addLightToRig(rig.id,
            type.subtype,
            [this](const ProjectOpReply &reply,
                const std::optional<LightAddedResult> &result) {
              if (!reply.ok) {
                reportError(reply.error);
                return;
              }
              // The node was pushed before the reply; select it.
              if (!result)
                return;
              auto *layer =
                  appContext()->vsr.scene.layer(result->lightNode.layerName);
              if (layer && result->lightNode.nodeIndex < layer->capacity()) {
                if (auto node = layer->at(result->lightNode.nodeIndex))
                  appContext()->setSelected(node);
              }
            });
      }
    }
    ImGui::EndPopup();
  }
  ImGui::EndDisabled();

  if (nodes.empty()) {
    ImGui::TextDisabled("No lights");
  } else {
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("lights", 3, flags)) {
      ImGui::TableSetupColumn(
          "", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Type");
      ImGui::TableHeadersRow();

      for (int i = 0; i < int(nodes.size()); ++i) {
        const bool selected = i == selectedLight;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        if (selected) {
          ImGui::TableSetBgColor(
              ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
        }
        ImGui::TableNextColumn();
        if (ImGui::RadioButton("##selected", selected))
          ctx->setSelected(nodes[i]);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(lightName(nodes[i]).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(lightSubtype(nodes[i]).c_str());
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }

  const bool hasSelection = selectedLight >= 0;
  ImGui::BeginDisabled(true);
  ImGui::Button("Rename Selected");
  ImGui::EndDisabled();
  vsr::ui::tooltipForPreviousItem(
      "Renaming a light is not in the protocol yet; edit the name in the"
      " Object Editor once it is");
  ImGui::SameLine();
  ImGui::BeginDisabled(!hasSelection || pending(m_pendingLightOp));
  if (ImGui::Button("Remove Selected") && hasSelection) {
    auto node = nodes[selectedLight];
    SceneNodeRef ref;
    ref.layerName = rig.rootNode.layerName;
    ref.nodeIndex = node.index();
    ctx->removeFromSelection(node);
    m_pendingLightOp = ops().removeLightFromRig(rig.id, ref, errorReporter());
  }
  ImGui::EndDisabled();
}

// Popups /////////////////////////////////////////////////////////////////////

void LightRigEditor::buildPopups(const Project &project)
{
  buildUI_removeConfirmation(project);
  m_browse.renderUI();
}

void LightRigEditor::buildUI_removeConfirmation(const Project &project)
{
  if (!ImGui::BeginPopupModal(
          REMOVE_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  const LightRig *rig = replica::findLightRig(project, m_rigToRemove);
  const size_t useCount = replica::lightRigUseCount(project, m_rigToRemove);
  ImGui::Text("Delete '%s' and clear %zu shot reference%s?",
      rig ? rig->name.c_str() : m_rigToRemove.c_str(),
      useCount,
      useCount == 1 ? "" : "s");

  ImGui::BeginDisabled(!canSend());
  if (ImGui::Button("Delete")) {
    m_pendingOp = ops().removeLightRig(m_rigToRemove, errorReporter());
    m_rigToRemove.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_rigToRemove.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace vsr::scivis_studio::client
