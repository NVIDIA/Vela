// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LightRigEditor.h"
// scivisStudioClient
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

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

constexpr const char *REMOVE_POPUP = "Delete Light Rig?";
constexpr const char *ADD_LIGHT_POPUP = "Add Light";

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
  m_nameField.markStale();
}

// Selection //////////////////////////////////////////////////////////////////

const LightRig *LightRigEditor::resolveSelection(const Project &project)
{
  if (!m_selectOnArrival.empty()) {
    if (light_rig::findLightRig(project, m_selectOnArrival)) {
      m_selected = m_selectOnArrival;
      m_selectOnArrival.clear();
    }
  }
  if (project.lightRigs.empty()) {
    m_selected.clear();
    return nullptr;
  }
  const LightRig *rig = light_rig::findLightRig(project, m_selected);
  if (!rig) {
    rig = &project.lightRigs.front();
    m_selected = rig->id;
  }
  return rig;
}

void LightRigEditor::syncSelectionToActiveShot(const Project &project)
{
  const Shot *shot = project::activeShot(project);
  const auto activeShotId = shot ? shot->id : ShotID{};
  const auto activeRigId = shot ? shot->lightRigId : LightRigID{};
  if (activeShotId == m_lastActiveShotId
      && activeRigId == m_lastActiveShotLightRigId)
    return;
  m_lastActiveShotId = activeShotId;
  m_lastActiveShotLightRigId = activeRigId;
  if (shot && light_rig::findLightRig(project, shot->lightRigId))
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
  rig = light_rig::findLightRig(project, m_selected);
  if (!rig)
    return;

  buildUI_nameField(*rig);
  buildUI_rigActions(project, *rig);
  buildUI_lightList(*rig);
}

void LightRigEditor::buildUI_toolbar(const Project &project)
{
  ProjectOps &ops = m_context->ops();
  auto createdReply = [this](const ProjectOpReply &reply,
                          const std::optional<LightRigCreatedResult> &result) {
    if (!reply.ok)
      m_context->error(reply.error);
    else if (result)
      m_selectOnArrival = result->lightRigId;
  };

  ImGui::BeginDisabled(m_rigOp.busy(ops));
  if (ImGui::Button("Add Rig")) {
    m_rigOp.sendForResult<LightRigCreatedResult>(
        ops, CreateLightRig{}, createdReply);
  }

  ImGui::SameLine();
  ImGui::BeginDisabled(m_selected.empty());
  if (ImGui::Button("Clone Rig")) {
    CloneLightRig clone;
    clone.lightRigId = m_selected;
    m_rigOp.sendForResult<LightRigCreatedResult>(
        ops, std::move(clone), createdReply);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Load Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenFile;
    request.title = "Load Light Rig Archive";
    request.extensions = ui::archiveExtensions();
    request.startDirectory = project.projectDirectory;
    request.onAccept = [this, createdReply](
                           const std::vector<std::filesystem::path> &paths) {
      LoadLightRigArchive load;
      load.file = paths.front();
      m_rigOp.sendForResult<LightRigCreatedResult>(
          m_context->ops(), std::move(load), createdReply);
    };
    m_browse.open(std::move(request));
  }
  ImGui::EndDisabled();
}

// The buffer travels as RenameLightRig; a refusal restores the replica's name.
void LightRigEditor::buildUI_nameField(const LightRig &rig)
{
  ProjectOps &ops = m_context->ops();
  const auto newName =
      m_nameField.draw(rig.id, rig.name, m_rename.busy(ops), "Invalid name: ");
  if (!newName)
    return;
  const LightRigID id = rig.id;
  RenameLightRig rename;
  rename.lightRigId = id;
  rename.newName = *newName;
  m_rename.send(
      ops, std::move(rename), [this, id](const ProjectOpReply &reply) {
        m_nameField.onReply(id, reply.ok, reply.error);
      });
}

void LightRigEditor::buildUI_rigActions(
    const Project &project, const LightRig &rig)
{
  ProjectOps &ops = m_context->ops();
  const Shot *shot = project::activeShot(project);
  const bool activeShotUsesRig = shot && shot->lightRigId == rig.id;

  ImGui::BeginDisabled(m_rigOp.busy(ops));

  ImGui::BeginDisabled(!shot || activeShotUsesRig);
  if (ImGui::Button("Use for Active Shot") && shot) {
    UpdateShot update;
    update.shotId = shot->id;
    update.patch.lightRigId = rig.id;
    m_rigOp.send(ops, std::move(update), m_context->errorReporter());
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Save Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::SaveFile;
    request.title = "Save Light Rig Archive";
    request.extensions = ui::archiveExtensions();
    request.startDirectory = project.projectDirectory;
    request.defaultName = (rig.name.empty() ? rig.id : rig.name) + ".vsr";
    const LightRigID id = rig.id;
    request.onAccept = [this, id](
                           const std::vector<std::filesystem::path> &paths) {
      SaveLightRigArchive save;
      save.lightRigId = id;
      save.file = ui::withVsrExtension(paths.front());
      m_rigOp.send(
          m_context->ops(), std::move(save), m_context->errorReporter());
    };
    m_browse.open(std::move(request));
  }

  ImGui::SameLine();
  if (ImGui::Button("Remove Rig")) {
    if (project::lightRigUseCount(project, rig.id) > 0) {
      m_rigToRemove = rig.id;
      ImGui::OpenPopup(REMOVE_POPUP);
    } else {
      RemoveLightRig remove;
      remove.lightRigId = rig.id;
      m_rigOp.send(ops, std::move(remove), m_context->errorReporter());
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
  ProjectOps &ops = m_context->ops();

  ImGui::BeginDisabled(m_lightOp.busy(ops));
  if (ImGui::Button("Add Light"))
    ImGui::OpenPopup(ADD_LIGHT_POPUP);
  if (ImGui::BeginPopup(ADD_LIGHT_POPUP)) {
    for (const auto &type : light_rig::LIGHT_SUBTYPES) {
      if (ImGui::MenuItem(type.label)) {
        AddLightToRig add;
        add.lightRigId = rig.id;
        add.subtype = type.subtype;
        m_lightOp.sendForResult<LightAddedResult>(ops,
            std::move(add),
            [this](const ProjectOpReply &reply,
                const std::optional<LightAddedResult> &result) {
              if (!reply.ok) {
                m_context->error(reply.error);
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
  ImGui::BeginDisabled(!hasSelection || m_lightOp.busy(ops));
  if (ImGui::Button("Remove Selected") && hasSelection) {
    auto node = nodes[selectedLight];
    RemoveLightFromRig remove;
    remove.lightRigId = rig.id;
    remove.lightNode.layerName = rig.rootNode.layerName;
    remove.lightNode.nodeIndex = node.index();
    ctx->removeFromSelection(node);
    m_lightOp.send(ops, std::move(remove), m_context->errorReporter());
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
  const LightRig *rig = light_rig::findLightRig(project, m_rigToRemove);
  const size_t useCount = project::lightRigUseCount(project, m_rigToRemove);
  const auto choice = ui::confirmModal(REMOVE_POPUP,
      "Delete '" + (rig ? rig->name : m_rigToRemove) + "' and clear "
          + std::to_string(useCount) + " shot reference"
          + (useCount == 1 ? "" : "s") + "?",
      "Delete",
      m_context->canSend());
  if (choice == ui::ConfirmChoice::Confirmed) {
    RemoveLightRig remove;
    remove.lightRigId = m_rigToRemove;
    m_rigOp.send(
        m_context->ops(), std::move(remove), m_context->errorReporter());
  }
  if (choice != ui::ConfirmChoice::Pending)
    m_rigToRemove.clear();
}

} // namespace vsr::scivis_studio::client
