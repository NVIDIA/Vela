// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// vsr_scene
#include <vsr/scene/Layer.hpp>
#include <vsr/scene/Object.hpp>

namespace vsr::demo {

struct InstancingControls : public vsr::ui::imgui::Window
{
  InstancingControls(vsr::ui::imgui::Application *app,
      const char *name = "Instancing Controls");

  void buildUI() override;

 private:
  void clearWorld();
  void createScene();
  void generateSpheres();
  void generateInstances();

  // Data //

  int m_numInstances{5000};
  float m_spacing{25.f};
  float m_particleRadius{0.5f};
  bool m_addSpheres{true};
  bool m_addInstances{true};
  vsr::scene::LayerNodeRef m_worldRoot;
  vsr::scene::Object *m_light{nullptr};
};

} // namespace vsr::demo
