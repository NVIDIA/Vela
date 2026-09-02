// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ShotEditor.h"
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

namespace vsr::scivis_studio::client {

namespace {

constexpr const char *NO_RENDERERS_LABEL = "<no renderers>";

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

} // namespace

ShotEditor::ShotEditor(vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Shot Editor")
{}

ShotEditor::~ShotEditor() = default;

void ShotEditor::onProjectReplaced()
{
  m_draftStale = true;
}

// Draft //////////////////////////////////////////////////////////////////////

void ShotEditor::syncDraft(const Project &project)
{
  const Shot *shot = replica::activeShot(project);
  if (!shot) {
    m_draft.reset();
    return;
  }
  const bool switched = !m_draft || m_draft->id != shot->id;
  // A stale draft waits for the in-flight update and for the user to let go
  // of whatever control they are on, so a refresh never yanks an edit away.
  const bool refresh = m_draftStale && !pending(m_pendingUpdate)
      && !ImGui::IsAnyItemActive();
  if (switched || refresh) {
    m_draft = *shot;
    m_draftStale = false;
  }
}

void ShotEditor::sendDraft()
{
  if (!m_draft || !canSend())
    return;

  Shot shot = *m_draft;
  shot.frameCount = std::max(1, shot.frameCount);
  shot.currentFrame = std::clamp(shot.currentFrame, 0, shot.frameCount - 1);
  shot.fps = std::max(1.f, shot.fps);
  shot.renderSettings.width = std::max(1u, shot.renderSettings.width);
  shot.renderSettings.height = std::max(1u, shot.renderSettings.height);
  shot.renderSettings.samples = std::max(1u, shot.renderSettings.samples);
  *m_draft = shot;

  m_pendingUpdate =
      ops().updateShot(shot, [this](const protocol::ProjectOpReply &reply) {
        if (!reply.ok)
          reportError(reply.error);
        // Success brings a snapshot; failure must snap the draft back.
        m_draftStale = true;
      });
}

// UI /////////////////////////////////////////////////////////////////////////

void ShotEditor::buildEditorUI(const Project &project)
{
  syncDraft(project);
  if (!m_draft) {
    ImGui::TextDisabled("No active shot");
    return;
  }
  Shot &shot = *m_draft;

  ImGui::BeginDisabled(pending(m_pendingUpdate));

  ImGui::InputText("Name", &shot.name);
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();

  ImGui::Text("Current frame: %d", shot.currentFrame);
  vsr::ui::tooltipForPreviousItem("Playback arrives in a later milestone");

  ImGui::InputInt("Frame count", &shot.frameCount);
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();
  ImGui::InputFloat("FPS", &shot.fps);
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();
  if (ImGui::Checkbox("Loop", &shot.loop))
    sendDraft();

  ImGui::SeparatorText("Render");
  int width = int(shot.renderSettings.width);
  int height = int(shot.renderSettings.height);
  int samples = int(shot.renderSettings.samples);
  if (ImGui::InputInt("Width", &width))
    shot.renderSettings.width = uint32_t(std::max(1, width));
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();
  if (ImGui::InputInt("Height", &height))
    shot.renderSettings.height = uint32_t(std::max(1, height));
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();
  if (ImGui::InputInt("Samples", &samples))
    shot.renderSettings.samples = uint32_t(std::max(1, samples));
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();

  buildUI_deviceSelector();
  buildUI_rendererSelector();

  ImGui::InputText("Output prefix", &shot.renderSettings.outputFilePrefix);
  if (ImGui::IsItemDeactivatedAfterEdit())
    sendDraft();

  buildUI_lightRigSelector(project);
  buildUI_cameraRigSelector(project);

  ImGui::Text("Output: renders/%s/", shot.id.c_str());

  buildUI_datasets(project);

  ImGui::EndDisabled();
}

void ShotEditor::buildUI_deviceSelector()
{
  auto &settings = m_draft->renderSettings;
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
      settings.rendererLibrary = library;
      settings.rendererObjectIndex = VSR_INVALID_INDEX;
      settings.rendererSubtype = "default";
      sendDraft();
    }
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  ImGui::EndCombo();
}

void ShotEditor::buildUI_rendererSelector()
{
  auto &settings = m_draft->renderSettings;
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
        settings.rendererObjectIndex = renderer->index();
        settings.rendererSubtype = renderer->subtype().str();
        sendDraft();
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
}

void ShotEditor::buildUI_lightRigSelector(const Project &project)
{
  Shot &shot = *m_draft;
  const std::string preview = shot.lightRigId.empty()
      ? std::string{"None"}
      : replica::lightRigLabel(project, shot.lightRigId);

  if (!ImGui::BeginCombo("Light Rig", preview.c_str()))
    return;
  const bool noneSelected = shot.lightRigId.empty();
  if (ImGui::Selectable("None", noneSelected) && !noneSelected) {
    shot.lightRigId.clear();
    sendDraft();
  }
  for (const LightRig *rig : replica::sortedLightRigs(project)) {
    const bool selected = shot.lightRigId == rig->id;
    if (ImGui::Selectable(rig->name.c_str(), selected) && !selected) {
      shot.lightRigId = rig->id;
      sendDraft();
    }
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  if (!shot.lightRigId.empty()
      && !replica::findLightRig(project, shot.lightRigId))
    ImGui::TextDisabled("%s", preview.c_str());
  ImGui::EndCombo();
}

void ShotEditor::buildUI_cameraRigSelector(const Project &project)
{
  Shot &shot = *m_draft;
  const std::string preview = shot.cameraRigId.empty()
      ? std::string{"None"}
      : replica::cameraRigLabel(project, shot.cameraRigId);

  if (!ImGui::BeginCombo("Camera Rig", preview.c_str()))
    return;
  const bool noneSelected = shot.cameraRigId.empty();
  if (ImGui::Selectable("None", noneSelected) && !noneSelected) {
    shot.cameraRigId.clear();
    sendDraft();
  }
  for (const CameraRig *rig : replica::sortedCameraRigs(project)) {
    const bool selected = shot.cameraRigId == rig->id;
    if (ImGui::Selectable(rig->name.c_str(), selected) && !selected) {
      shot.cameraRigId = rig->id;
      sendDraft();
    }
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  if (!shot.cameraRigId.empty()
      && !replica::findCameraRig(project, shot.cameraRigId))
    ImGui::TextDisabled("%s", preview.c_str());
  ImGui::EndCombo();
}

void ShotEditor::buildUI_datasets(const Project &project)
{
  ImGui::SeparatorText("Datasets");
  if (project.datasets.empty())
    ImGui::TextDisabled("No datasets");
  for (const auto &dataset : project.datasets) {
    bool enabled = true;
    if (const auto *binding = shot::findDatasetBinding(*m_draft, dataset.id))
      enabled = binding->enabled;
    ImGui::PushID(dataset.id.c_str());
    if (ImGui::Checkbox(dataset.name.c_str(), &enabled)) {
      shot::setDatasetBinding(*m_draft, dataset.id, enabled);
      sendDraft();
    }
    ImGui::PopID();
  }
}

} // namespace vsr::scivis_studio::client
