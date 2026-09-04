// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ShotEditor.h"

#include "imgui.h"
#include "vsr/app/Context.h"
#include "vsr/core/Logging.hpp"
#include "vsr/scene/objects/Renderer.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace vsr::scivis_studio {

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

} // namespace

ShotEditor::ShotEditor(vsr::ui::imgui::Application *app,
    ProjectContext *projectContext,
    std::function<void()> onRender)
    : Window(app, "Shot Editor"),
      m_projectContext(projectContext),
      m_onRender(std::move(onRender))
{}

ShotEditor::~ShotEditor() = default;

bool ShotEditor::inputText(
    const char *label, std::string &value, size_t capacity)
{
  std::vector<char> buffer(capacity, '\0');
  std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
  if (ImGui::InputText(label, buffer.data(), buffer.size())) {
    value = buffer.data();
    return true;
  }
  return false;
}

// A size or count the widget shows as int; a non-positive entry reads as 1,
// the floor clampToValidRanges() applies, so the unsigned field never wraps.
bool ShotEditor::inputSize(const char *label, uint32_t &value)
{
  int shown = static_cast<int>(value);
  if (!ImGui::InputInt(label, &shown))
    return false;
  value = static_cast<uint32_t>(std::max(1, shown));
  return true;
}

bool ShotEditor::buildUI_deviceSelector(Shot &shot)
{
  auto *ctx = m_projectContext ? m_projectContext->appContext() : nullptr;
  auto &settings = shot.renderSettings;
  bool edited = false;
  const auto preview = settings.rendererLibrary.empty()
      ? std::string{"<none>"}
      : settings.rendererLibrary;

  if (!ctx) {
    ImGui::BeginDisabled();
    if (ImGui::BeginCombo("Device", preview.c_str()))
      ImGui::EndCombo();
    ImGui::EndDisabled();
    return false;
  }

  if (ImGui::BeginCombo("Device", preview.c_str())) {
    for (const auto &libName : ctx->anari.libraryList()) {
      const bool selected = settings.rendererLibrary == libName;
      if (ImGui::Selectable(libName.c_str(), selected)) {
        if (settings.rendererLibrary != libName) {
          settings.rendererLibrary = libName;
          settings.rendererObjectIndex = VSR_INVALID_INDEX;
          settings.rendererSubtype = "default";
          m_rendererLoadAttemptedLibrary.clear();
          edited = true;
        }
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return edited;
}

bool ShotEditor::buildUI_rendererSelector(Shot &shot)
{
  auto *ctx = m_projectContext ? m_projectContext->appContext() : nullptr;
  auto &settings = shot.renderSettings;
  bool edited = false;
  std::vector<vsr::scene::RendererAppRef> renderers;

  if (ctx && ctx->anari.isLoadableLibrary(settings.rendererLibrary)) {
    auto &scene = ctx->vsr.scene;
    renderers = scene.renderersOfDevice(settings.rendererLibrary);
    if (renderers.empty()
        && m_rendererLoadAttemptedLibrary != settings.rendererLibrary) {
      m_rendererLoadAttemptedLibrary = settings.rendererLibrary;
      if (auto device = ctx->anari.loadDevice(settings.rendererLibrary)) {
        renderers =
            scene.createStandardRenderers(settings.rendererLibrary, device);
        anari::release(device, device);
      } else {
        vsr::core::logWarning(
            "[SciVisStudio] failed to load ANARI device '%s' for shot "
            "renderer selection",
            settings.rendererLibrary.c_str());
      }
    }
  }

  vsr::scene::RendererAppRef currentRenderer;
  if (ctx && settings.rendererObjectIndex != VSR_INVALID_INDEX) {
    auto renderer = ctx->vsr.scene.getObject<vsr::scene::Renderer>(
        settings.rendererObjectIndex);
    if (renderer && renderer->rendererDeviceName() == settings.rendererLibrary)
      currentRenderer = renderer;
  }

  if (!currentRenderer && !renderers.empty()) {
    currentRenderer = renderers.front();
    if (settings.rendererObjectIndex != currentRenderer->index()
        || settings.rendererSubtype != currentRenderer->subtype().str()) {
      settings.rendererObjectIndex = currentRenderer->index();
      settings.rendererSubtype = currentRenderer->subtype().str();
      edited = true;
    }
  }

  const auto preview = currentRenderer ? rendererLabel(*currentRenderer)
                                       : std::string{NO_RENDERERS_LABEL};
  ImGui::BeginDisabled(renderers.empty());
  if (ImGui::BeginCombo("Renderer", preview.c_str())) {
    for (const auto &renderer : renderers) {
      if (!renderer)
        continue;
      const bool selected = renderer->index() == settings.rendererObjectIndex;
      const auto label = rendererLabel(*renderer);
      if (ImGui::Selectable(label.c_str(), selected)) {
        if (settings.rendererObjectIndex != renderer->index()
            || settings.rendererSubtype != renderer->subtype().str()) {
          settings.rendererObjectIndex = renderer->index();
          settings.rendererSubtype = renderer->subtype().str();
          edited = true;
        }
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  return edited;
}

bool ShotEditor::buildUI_lightRigSelector(Shot &shot)
{
  const auto &project = m_projectContext->project();
  bool edited = false;
  std::string preview = "None";
  if (!shot.lightRigId.empty()) {
    if (auto *rig = light_rig::findLightRig(project, shot.lightRigId))
      preview = rig->name;
    else
      preview = "<missing: " + shot.lightRigId + ">";
  }

  if (ImGui::BeginCombo("Light Rig", preview.c_str())) {
    const bool noneSelected = shot.lightRigId.empty();
    if (ImGui::Selectable("None", noneSelected)) {
      if (!shot.lightRigId.empty()) {
        shot.lightRigId.clear();
        edited = true;
      }
    }
    if (noneSelected)
      ImGui::SetItemDefaultFocus();

    for (const auto &rig : project.lightRigs) {
      const bool selected = shot.lightRigId == rig.id;
      if (ImGui::Selectable(rig.name.c_str(), selected)) {
        if (shot.lightRigId != rig.id) {
          shot.lightRigId = rig.id;
          edited = true;
        }
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }

    if (!shot.lightRigId.empty()
        && !light_rig::findLightRig(project, shot.lightRigId)) {
      const auto missing = "<missing: " + shot.lightRigId + ">";
      ImGui::TextDisabled("%s", missing.c_str());
    }
    ImGui::EndCombo();
  }
  return edited;
}

bool ShotEditor::buildUI_cameraRigSelector(Shot &shot)
{
  const auto &project = m_projectContext->project();
  bool edited = false;
  std::string preview = "None";
  if (!shot.cameraRigId.empty()) {
    if (auto *rig = camera_rig::findCameraRig(project, shot.cameraRigId))
      preview = rig->name;
    else
      preview = "<missing: " + shot.cameraRigId + ">";
  }

  if (ImGui::BeginCombo("Camera Rig", preview.c_str())) {
    const bool noneSelected = shot.cameraRigId.empty();
    if (ImGui::Selectable("None", noneSelected)) {
      if (!shot.cameraRigId.empty()) {
        shot.cameraRigId.clear();
        edited = true;
      }
    }
    if (noneSelected)
      ImGui::SetItemDefaultFocus();

    for (const auto &rig : project.cameraRigs) {
      const bool selected = shot.cameraRigId == rig.id;
      if (ImGui::Selectable(rig.name.c_str(), selected)) {
        if (shot.cameraRigId != rig.id) {
          shot.cameraRigId = rig.id;
          edited = true;
        }
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }

    if (!shot.cameraRigId.empty()
        && !camera_rig::findCameraRig(project, shot.cameraRigId)) {
      const auto missing = "<missing: " + shot.cameraRigId + ">";
      ImGui::TextDisabled("%s", missing.c_str());
    }
    ImGui::EndCombo();
  }
  return edited;
}

void ShotEditor::buildUI()
{
  if (!m_projectContext)
    return;

  auto &project = m_projectContext->project();
  const auto *active = project::activeShot(project);
  if (!active) {
    ImGui::TextDisabled("No active shot");
    return;
  }
  auto *ctx = m_projectContext->appContext();

  // The widgets edit a copy of the active shot; whatever changed this frame
  // lands through one updateShot(), whose validation, clamps and dirty
  // marking are the only ones. Playback (frame, play/stop) goes through its
  // own ops and never through the copy.
  Shot shot = *active;
  bool edited = false;

  edited |= inputText("Name", shot.name);

  int currentFrame = shot.currentFrame;
  if (ImGui::InputInt("Current frame", &currentFrame)) {
    m_projectContext->setActiveShotFrame(currentFrame);
    shot.currentFrame = active->currentFrame;
  }
  edited |= ImGui::InputInt("Frame count", &shot.frameCount);
  edited |= ImGui::InputFloat("FPS", &shot.fps);

  const bool playing = ctx ? ctx->vsr.animationMgr.isPlaying() : shot.playing;
  if (ImGui::Button(playing ? "Stop" : "Play"))
    m_projectContext->setPlaying(shot.id, !playing);
  ImGui::SameLine();
  edited |= ImGui::Checkbox("Loop", &shot.loop);

  ImGui::SeparatorText("Render");
  edited |= inputSize("Width", shot.renderSettings.width);
  edited |= inputSize("Height", shot.renderSettings.height);
  edited |= inputSize("Samples", shot.renderSettings.samples);
  edited |= buildUI_deviceSelector(shot);
  edited |= buildUI_rendererSelector(shot);
  edited |= inputText("Output prefix", shot.renderSettings.outputFilePrefix);
  edited |= buildUI_lightRigSelector(shot);
  edited |= buildUI_cameraRigSelector(shot);

  ImGui::Text("Output: renders/%s/", shot.id.c_str());
  if (ImGui::Button("Render Active Shot") && m_onRender)
    m_onRender();

  ImGui::SeparatorText("Datasets");
  for (const auto &dataset : project.datasets) {
    bool enabled = true;
    if (auto *binding = shot::findDatasetBinding(shot, dataset.id))
      enabled = binding->enabled;
    if (ImGui::Checkbox(dataset.name.c_str(), &enabled)) {
      shot::setDatasetBinding(shot, dataset.id, enabled);
      edited = true;
    }
  }

  if (!edited)
    return;
  std::string error;
  if (m_projectContext->updateShot(shot, &error)) {
    m_lastRejection.clear();
    return;
  }
  // The renderer selector's fix-up edits every frame until it lands, so a
  // shot the op keeps refusing (a rig id no rig has) would log every frame;
  // once per distinct reason is enough.
  if (error != m_lastRejection) {
    m_lastRejection = error;
    vsr::core::logWarning(
        "[SciVisStudio] shot edit rejected: %s", error.c_str());
  }
}

} // namespace vsr::scivis_studio
