// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ShotEditor.h"
// scivisStudioClient
#include "UICommon.h"
// vsr_scivis_studio_client_core
#include "ReplicaView.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Renderer.hpp"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <vector>

namespace vsr::scivis_studio::client {

namespace {

constexpr const char *NO_RENDERERS_LABEL = "<no renderers>";
constexpr const char *RENDER_SHOT_POPUP = "Render Shot?";

std::string rendererLabel(const vsr::scene::Renderer &renderer)
{
  std::string label = renderer.name();
  if (label.empty())
    label = renderer.subtype().str();
  label += " [" + std::to_string(renderer.index()) + "]";
  return label;
}

// Every device the mirror holds a Renderer for, plus `current` so the shot's
// own choice stays selectable even when the mirror has no renderer for it.
std::vector<std::string> rendererLibraries(
    const vsr::scene::Scene &scene, const std::string &current)
{
  std::vector<std::string> libraries;
  const size_t count = scene.numberOfObjects(ANARI_RENDERER);
  for (size_t i = 0; i < count; ++i) {
    auto renderer = scene.getObject<vsr::scene::Renderer>(i);
    if (!renderer)
      continue;
    const auto library = renderer->rendererDeviceName().str();
    if (library.empty())
      continue;
    if (std::find(libraries.begin(), libraries.end(), library)
        == libraries.end())
      libraries.push_back(library);
  }
  if (!current.empty()
      && std::find(libraries.begin(), libraries.end(), current)
          == libraries.end())
    libraries.push_back(current);
  std::sort(libraries.begin(), libraries.end());
  return libraries;
}

// A size field: edited as an int, committed on deactivation as at least 1.
// True with `out` set when the field just committed.
bool inputSize(
    ui::IntField &field, const char *label, uint32_t current, uint32_t &out)
{
  int value = 0;
  if (!field.draw(label, int(current), value))
    return false;
  out = uint32_t(std::max(1, value));
  return true;
}

} // namespace

ShotEditor::ShotEditor(vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Shot Editor")
{}

ShotEditor::~ShotEditor() = default;

void ShotEditor::commit(const Shot &shot, const ShotPatch &patch)
{
  if (!canSend())
    return;
  m_pendingUpdate = ops().updateShot(shot.id, patch, errorReporter());
}

// UI /////////////////////////////////////////////////////////////////////////

void ShotEditor::buildEditorUI(const Project &project)
{
  const Shot *shot = replica::activeShot(project);
  if (!shot) {
    ImGui::TextDisabled("No active shot");
    return;
  }

  ImGui::BeginDisabled(pending(m_pendingUpdate));

  // Each control edits a copy of its field for this UI frame and commits a
  // patch of that field alone; ImGui (or an IntField) holds an edit in
  // progress, so a snapshot landing meanwhile does not yank it away.
  std::string name = shot->name;
  ImGui::InputText("Name", &name);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    ShotPatch patch;
    patch.name = name;
    commit(*shot, patch);
  }

  buildUI_playback(*shot);

  ImGui::SeparatorText("Render");
  buildUI_renderSettings(*shot);
  buildUI_deviceSelector(*shot);
  buildUI_rendererSelector(*shot);

  std::string prefix = shot->renderSettings.outputFilePrefix;
  ImGui::InputText("Output prefix", &prefix);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    ShotPatch patch;
    patch.renderSettings.outputFilePrefix = prefix;
    commit(*shot, patch);
  }

  buildUI_lightRigSelector(project, *shot);
  buildUI_cameraRigSelector(project, *shot);

  ImGui::Text("Output: renders/%s/", shot->id.c_str());
  buildUI_render(project, *shot);

  buildUI_datasets(project, *shot);

  ImGui::EndDisabled();
}

void ShotEditor::buildUI_playback(const Shot &shot)
{
  int frameCount = 0;
  if (m_frameCountField.draw("Frame count", shot.frameCount, frameCount)) {
    ShotPatch patch;
    patch.frameCount = frameCount;
    commit(shot, patch);
  }
  float fps = shot.fps;
  ImGui::InputFloat("FPS", &fps);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    ShotPatch patch;
    patch.fps = fps;
    commit(shot, patch);
  }
  bool loop = shot.loop;
  if (ImGui::Checkbox("Loop", &loop)) {
    ShotPatch patch;
    patch.loop = loop;
    commit(shot, patch);
  }
}

void ShotEditor::buildUI_renderSettings(const Shot &shot)
{
  const auto &settings = shot.renderSettings;
  uint32_t size = 0;
  if (inputSize(m_widthField, "Width", settings.width, size)) {
    ShotPatch patch;
    patch.renderSettings.width = size;
    commit(shot, patch);
  }
  if (inputSize(m_heightField, "Height", settings.height, size)) {
    ShotPatch patch;
    patch.renderSettings.height = size;
    commit(shot, patch);
  }
  if (inputSize(m_samplesField, "Samples", settings.samples, size)) {
    ShotPatch patch;
    patch.renderSettings.samples = size;
    commit(shot, patch);
  }
}

void ShotEditor::buildUI_render(const Project &project, const Shot &shot)
{
  const bool busy = pending(m_pendingRender) || m_context->renderInProgress();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Render Shot..."))
    m_shotToRender = shot.id;
  ImGui::EndDisabled();
  if (project.dirty) {
    ImGui::SameLine();
    ImGui::TextDisabled("(save the project first)");
  }
}

void ShotEditor::buildPopups(const Project &project)
{
  if (m_shotToRender.empty())
    return;
  const Shot *shot = replica::findShot(project, m_shotToRender);
  if (!shot) {
    m_shotToRender.clear(); // gone with a snapshot
    return;
  }
  if (!ImGui::IsPopupOpen(RENDER_SHOT_POPUP))
    ImGui::OpenPopup(RENDER_SHOT_POPUP);
  const auto choice = ui::confirmModal(RENDER_SHOT_POPUP,
      "Render " + std::to_string(std::max(0, shot->frameCount))
          + " frame(s) of '" + shot->name + "' to renders/" + shot->id
          + "/ on the server?",
      "Render",
      canSend(),
      [&] {
        if (project.dirty)
          ui::warningText(
              "The project has unsaved changes; the server will"
              " refuse until it is saved.");
        ImGui::TextDisabled(
            "The shot becomes the active shot; edits are"
            " refused while the render runs.");
      });
  if (choice == ui::ConfirmChoice::Confirmed) {
    m_pendingRender = ops().renderShot(m_shotToRender,
        [this](const protocol::ProjectOpReply &reply,
            const std::optional<protocol::TaskStartedResult> &) {
          if (!reply.ok)
            reportError(reply.error);
        });
  }
  if (choice != ui::ConfirmChoice::Pending)
    m_shotToRender.clear();
}

void ShotEditor::buildUI_deviceSelector(const Shot &shot)
{
  const auto &settings = shot.renderSettings;
  const auto &scene = appContext()->vsr.scene;
  const auto libraries = rendererLibraries(scene, settings.rendererLibrary);
  const std::string preview = settings.rendererLibrary.empty()
      ? std::string{"<none>"}
      : settings.rendererLibrary;

  if (!ImGui::BeginCombo("Device", preview.c_str()))
    return;
  if (libraries.empty())
    ImGui::TextDisabled("<the mirror holds no renderers>");
  for (const auto &library : libraries) {
    const bool selected = settings.rendererLibrary == library;
    if (ImGui::Selectable(library.c_str(), selected) && !selected) {
      // A device pick is one edit across the three renderer fields: the
      // renderer of the old device cannot stand for the new one.
      ShotPatch patch;
      patch.renderSettings.rendererLibrary = library;
      patch.renderSettings.rendererObjectIndex = VSR_INVALID_INDEX;
      patch.renderSettings.rendererSubtype = "default";
      commit(shot, patch);
    }
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  ImGui::EndCombo();
}

void ShotEditor::buildUI_rendererSelector(const Shot &shot)
{
  const auto &settings = shot.renderSettings;
  auto &scene = appContext()->vsr.scene;
  const auto renderers = settings.rendererLibrary.empty()
      ? std::vector<vsr::scene::RendererAppRef>{}
      : scene.renderersOfDevice(settings.rendererLibrary);

  vsr::scene::RendererAppRef current;
  if (settings.rendererObjectIndex != VSR_INVALID_INDEX) {
    auto renderer =
        scene.getObject<vsr::scene::Renderer>(settings.rendererObjectIndex);
    if (renderer
        && renderer->rendererDeviceName().str() == settings.rendererLibrary)
      current = renderer;
  }

  const std::string preview =
      current ? rendererLabel(*current) : std::string{NO_RENDERERS_LABEL};
  ImGui::BeginDisabled(renderers.empty());
  if (ImGui::BeginCombo("Renderer", preview.c_str())) {
    for (const auto &renderer : renderers) {
      if (!renderer)
        continue;
      const bool selected = renderer->index() == settings.rendererObjectIndex;
      const auto label = rendererLabel(*renderer);
      if (ImGui::Selectable(label.c_str(), selected) && !selected) {
        ShotPatch patch;
        patch.renderSettings.rendererObjectIndex = renderer->index();
        patch.renderSettings.rendererSubtype = renderer->subtype().str();
        commit(shot, patch);
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
}

void ShotEditor::buildUI_lightRigSelector(
    const Project &project, const Shot &shot)
{
  const std::string preview = shot.lightRigId.empty()
      ? std::string{"None"}
      : replica::lightRigLabel(project, shot.lightRigId);

  if (!ImGui::BeginCombo("Light Rig", preview.c_str()))
    return;
  const auto pick = [&](const LightRigID &id) {
    ShotPatch patch;
    patch.lightRigId = id;
    commit(shot, patch);
  };
  const bool noneSelected = shot.lightRigId.empty();
  if (ImGui::Selectable("None", noneSelected) && !noneSelected)
    pick({});
  for (const LightRig *rig : replica::sortedLightRigs(project)) {
    const bool selected = shot.lightRigId == rig->id;
    if (ImGui::Selectable(rig->name.c_str(), selected) && !selected)
      pick(rig->id);
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  if (!shot.lightRigId.empty()
      && !replica::findLightRig(project, shot.lightRigId))
    ImGui::TextDisabled("%s", preview.c_str());
  ImGui::EndCombo();
}

void ShotEditor::buildUI_cameraRigSelector(
    const Project &project, const Shot &shot)
{
  const std::string preview = shot.cameraRigId.empty()
      ? std::string{"None"}
      : replica::cameraRigLabel(project, shot.cameraRigId);

  if (!ImGui::BeginCombo("Camera Rig", preview.c_str()))
    return;
  const auto pick = [&](const CameraRigID &id) {
    ShotPatch patch;
    patch.cameraRigId = id;
    commit(shot, patch);
  };
  const bool noneSelected = shot.cameraRigId.empty();
  if (ImGui::Selectable("None", noneSelected) && !noneSelected)
    pick({});
  for (const CameraRig *rig : replica::sortedCameraRigs(project)) {
    const bool selected = shot.cameraRigId == rig->id;
    if (ImGui::Selectable(rig->name.c_str(), selected) && !selected)
      pick(rig->id);
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  if (!shot.cameraRigId.empty()
      && !replica::findCameraRig(project, shot.cameraRigId))
    ImGui::TextDisabled("%s", preview.c_str());
  ImGui::EndCombo();
}

void ShotEditor::buildUI_datasets(const Project &project, const Shot &shot)
{
  ImGui::SeparatorText("Datasets");
  if (project.datasets.empty())
    ImGui::TextDisabled("No datasets");
  for (const auto &dataset : project.datasets) {
    bool enabled = true;
    if (const auto *binding = shot::findDatasetBinding(shot, dataset.id))
      enabled = binding->enabled;
    ImGui::PushID(dataset.id.c_str());
    if (ImGui::Checkbox(dataset.name.c_str(), &enabled)) {
      ShotPatch patch;
      patch.datasetBindings.push_back({dataset.id, enabled});
      commit(shot, patch);
    }
    ImGui::PopID();
  }
}

} // namespace vsr::scivis_studio::client
