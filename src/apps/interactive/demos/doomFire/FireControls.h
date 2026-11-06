// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// vsr_scene
#include <vsr/scene/objects/Array.hpp>
#include <vsr/scene/objects/SpatialField.hpp>

#include "doomFire3D.h"

namespace vsr::demo {

struct FireControls : public vsr::ui::imgui::Window
{
  FireControls(
      vsr::ui::imgui::Application *app, const char *name = "Fire Controls");

  void buildUI() override;
  void setField(vsr::scene::SpatialFieldRef f);

 private:
  void remakeDataArray();
  void resetFire();
  void stepFire();
  void uploadToArray();

  vsr::scene::ObjectUsePtr<vsr::scene::SpatialField> m_field;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_data;
  DoomFire3D m_fire;
  DoomFire3D::Params m_params;

  vsr::math::int3 m_dims{128, 128, 128};
  int m_baseIntensity{255};
  float m_windDirectionDeg{0.f}; // in the XZ plane, 0 = +X, 90 = +Z
  float m_windStrength{0.f}; // cells per layer
  int m_stepsPerFrame{1};
  int m_totalSteps{0};
  bool m_playing{true};
  bool m_useGPUInterop{false};
};

} // namespace vsr::demo
