// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_app
#include <vsr/app/Context.h>
// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// std
#include <functional>
#include <utility>

#include "particle_system.h"

namespace vsr::demo {

struct SimulationControls : public vsr::ui::imgui::Window
{
  SimulationControls(vsr::ui::imgui::Application *app,
      const char *name = "Simulation Controls");

  void buildUI() override;
  void setGeometry(vsr::scene::GeometryRef particles,
      vsr::scene::GeometryRef blackHoles,
      vsr::scene::SamplerRef particleColorSampler);

 private:
  void remakeDataArrays();
  void resetSimulation();
  void updateColorMapScale();
  std::pair<vsr::math::float3, vsr::math::float3> updateBhPoints();
  void iterateSimulation();

  vsr::scene::ObjectUsePtr<vsr::scene::Geometry> m_particleGeom;
  vsr::scene::ObjectUsePtr<vsr::scene::Geometry> m_bhGeom;
  vsr::scene::ObjectUsePtr<vsr::scene::Sampler> m_particleColorSampler;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataPoints;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataPointsCUDA;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataDistances;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataDistancesCUDA;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataVelocities;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataVelocitiesCUDA;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataBhPoints;
  int m_particlesPerSide{100};
  vsr::demo::ParticleSystemParameters m_params;
  float m_angle{0.f};
  float m_rotationSpeed{35.f};
  float m_colorMapScaleFactor{3.f};
  bool m_playing{false};
  bool m_useGPUInterop{true};
  bool m_randomizeInitialVelocities{true};
};

} // namespace vsr::demo
