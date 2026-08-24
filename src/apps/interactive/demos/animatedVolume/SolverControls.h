// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// vsr_core
#include <vsr/scene/objects/Array.hpp>
#include <vsr/scene/objects/SpatialField.hpp>
// std
#include <functional>

namespace vsr::demo {

using JacobiUpdateCallback = std::function<void()>;

struct SolverControls : public vsr::ui::imgui::Window
{
  SolverControls(
      vsr::ui::imgui::Application *app, const char *name = "Solver Controls");

  void buildUI() override;
  void setField(vsr::scene::SpatialFieldRef f);
  void setUpdateCallback(JacobiUpdateCallback cb);

 private:
  void remakeDataArray();
  void resetSolver();
  void iterateSolver();
  void exportRAW();

  vsr::scene::ObjectUsePtr<vsr::scene::SpatialField> m_field;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataHost;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataCUDA_1;
  vsr::scene::ObjectUsePtr<vsr::scene::Array> m_dataCUDA_2;
  int m_iterationsPerCycle{2};
  vsr::math::int3 m_dims{256, 256, 256};
  int m_totalIterations{0};
  JacobiUpdateCallback m_cb;
  bool m_playing{false};
  bool m_useGPUInterop{false};
  bool m_updateTF{true};
  bool m_dumpVolumes{false};
  std::string m_exportRoot{"./"}; // root filename for .raw exports
};

} // namespace vsr::demo
