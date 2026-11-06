// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FireControls.h"
// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/vsr_ui_imgui.h>
// vsr_core
#include <vsr/core/Logging.hpp>
// std
#include <algorithm>
#include <cmath>

namespace vsr::demo {

FireControls::FireControls(vsr::ui::imgui::Application *app, const char *name)
    : vsr::ui::imgui::Window(app, name)
{}

void FireControls::buildUI()
{
  if (!m_field) {
    ImGui::Text("{fire window not setup correctly}");
    return;
  }

  if (ImGui::Button(m_playing ? "pause" : "play"))
    m_playing = !m_playing;

  ImGui::SameLine();
  ImGui::BeginDisabled(m_playing);
  const bool stepOnce = ImGui::Button("step");
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("reset"))
    resetFire();

  ImGui::SameLine();
  ImGui::Text("| step %i", m_totalSteps);

  ImGui::Separator();

  // Grid size: applied once the user lets go, so dragging doesn't reallocate
  // the grid every frame.
  ImGui::DragInt3("grid size (x, y-up, z)", &m_dims.x, 1, 8, 1024);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    m_dims.x = std::clamp(m_dims.x, 8, 1024);
    m_dims.y = std::clamp(m_dims.y, 8, 1024);
    m_dims.z = std::clamp(m_dims.z, 8, 1024);
    remakeDataArray();
  }

  ImGui::SliderInt("base intensity", &m_baseIntensity, 0, 255);
  ImGui::SameLine();
  ImGui::TextDisabled("(%.3f)", m_baseIntensity / 255.f);

  ImGui::SliderFloat(
      "wind direction", &m_windDirectionDeg, 0.f, 360.f, "%.0f deg");
  ImGui::SliderFloat("wind strength", &m_windStrength, 0.f, 4.f, "%.2f");
  ImGui::SliderInt("max decay", &m_params.maxDecay, 0, 32);
  ImGui::Checkbox("sideways jitter", &m_params.jitter);
  ImGui::SliderInt("steps per frame", &m_stepsPerFrame, 1, 16);

  ImGui::BeginDisabled(m_totalSteps > 0);
  if (ImGui::Checkbox("use GPU array interop", &m_useGPUInterop))
    remakeDataArray();
  ImGui::EndDisabled();

  if (m_playing || stepOnce)
    stepFire();
}

void FireControls::setField(vsr::scene::SpatialFieldRef f)
{
  m_field = f;
  remakeDataArray();
}

void FireControls::remakeDataArray()
{
  if (!m_field) {
    vsr::core::logWarning("[doom fire] no spatial field set");
    return;
  }

  auto &scene = appContext()->vsr.scene;

  m_data = {};
  scene.removeUnusedObjects();

  m_data = m_useGPUInterop
      ? scene.createArrayCUDA(ANARI_UFIXED8, m_dims.x, m_dims.y, m_dims.z)
      : scene.createArray(ANARI_UFIXED8, m_dims.x, m_dims.y, m_dims.z);

  const int maxDim = std::max({m_dims.x, m_dims.y, m_dims.z});
  m_field->setParameter("spacing", vsr::math::float3(2.f / maxDim));

  m_fire.resize(m_dims.x, m_dims.y, m_dims.z, uint8_t(m_baseIntensity));
  uploadToArray();
  m_field->setParameterObject("data", *m_data);

  m_totalSteps = 0;
}

void FireControls::resetFire()
{
  m_fire.reset(uint8_t(m_baseIntensity));
  uploadToArray();
  m_totalSteps = 0;
}

void FireControls::stepFire()
{
  const float rad = m_windDirectionDeg * float(M_PI) / 180.f;
  m_params.baseIntensity = uint8_t(m_baseIntensity);
  m_params.windX = m_windStrength * std::cos(rad);
  m_params.windZ = m_windStrength * std::sin(rad);

  m_fire.step(m_params, m_stepsPerFrame);
  uploadToArray();
  m_totalSteps += m_stepsPerFrame;
}

void FireControls::uploadToArray()
{
  if (!m_data)
    return;
  m_fire.copyTo(m_data->map());
  m_data->unmap();
}

} // namespace vsr::demo
