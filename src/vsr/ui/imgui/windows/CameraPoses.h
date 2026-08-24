// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr
#include <atomic>
#include <future>
#include <mutex>
#include "vsr/app/Context.h"
#include "vsr/core/VSRMath.hpp"
#include "vsr/core/Timer.hpp"
#include "vsr/rendering/view/CameraPath.h"

namespace vsr::ui::imgui {

struct Viewport;

struct CameraPoses : public Window
{
  CameraPoses(Application *app,
      const char *name = "Camera Poses");
  void buildUI() override;

 private:
  void buildUI_turntablePopupMenu();
  void buildUI_confirmPopupMenu();
  void buildUI_interpolationControls();
  void renderInterpolatedPath();

  vsr::math::float3 m_turntableCenter{0.f, 0.f, 0.f};
  vsr::math::float3 m_turntableAzimuths{0.f, 360.f, 20.f};
  vsr::math::float3 m_turntableElevations{5.f, 45.f, 10.f};
  float m_turntableDistance{1.f};

  bool m_updateViewport{true}; // Update viewport during rendering
  bool m_isRendering{false};
  bool m_cancelRequested{false};
  std::future<void> m_renderFuture;
  vsr::core::Timer m_renderTimer;
  int m_currentFrame{0};
  int m_totalFrames{0};
  std::atomic<bool> m_hasNewPose{false};
  vsr::rendering::CameraPose m_currentPose;
  std::mutex m_poseMutex;
};

} // namespace vsr::ui::imgui
