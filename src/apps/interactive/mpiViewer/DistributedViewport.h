// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// SDL
#include <SDL3/SDL.h>
// std
#include <array>
#include <limits>
// vsr_core
#include <vsr/scene/Object.hpp>
#include <vsr/scene/UpdateDelegate.hpp>
// vsr_app
#include <vsr/app/Context.h>
// vsr_rendering
#include <vsr/rendering/view/Manipulator.hpp>
// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/windows/Window.h>

#include "DistributedSceneController.h"

namespace vsr::mpi_viewer {

struct DistributedViewport : public vsr::ui::imgui::Window
{
  DistributedViewport(vsr::ui::imgui::Application *app,
      DistributedSceneController *dapp,
      const char *name = "Viewport");
  ~DistributedViewport();

  void buildUI() override;

  void setManipulator(vsr::rendering::Manipulator *m);
  void resetView(bool resetAzEl = true);

 private:
  void reshape(vsr::math::int2 newWindowSize);

  void updateCamera(bool force = false);
  void updateImage();

  void ui_handleInput();
  void ui_menuBar();
  void ui_overlay();
  void ui_timeControls();

  int windowFlags() const override;
  int pushStyle() override;

  // Data /////////////////////////////////////////////////////////////////////

  DistributedSceneController *m_dapp{nullptr};

  vsr::math::float2 m_previousMouse{-1.f, -1.f};
  bool m_mouseRotating{false};
  bool m_manipulating{false};
  bool m_ctxMenuVisible{false};
  bool m_saveNextFrame{false};
  int m_screenshotIndex{0};
  bool m_showOverlay{true};
  bool m_showTimeline{true};

  // ANARI objects //

  anari::DataType m_format{ANARI_UFIXED8_RGBA_SRGB};
  LocalState m_localState;

  // camera manipulator

  int m_arcballUp{1};
  vsr::rendering::Manipulator m_localArcball;
  vsr::rendering::Manipulator *m_arcball{nullptr};
  vsr::rendering::UpdateToken m_cameraToken{0};
  float m_fov{40.f};
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};

  // display

  SDL_Texture *m_framebufferTexture{nullptr};
  vsr::math::int2 m_viewportSize{1920, 1080};
  vsr::math::int2 m_renderSize{1920, 1080};
  float m_resolutionScale{1.f};

  float m_latestFL{1.f};
  float m_minFL{std::numeric_limits<float>::max()};
  float m_maxFL{-std::numeric_limits<float>::max()};

  std::string m_overlayWindowName;
  std::string m_ctxMenuName;
};

} // namespace vsr::mpi_viewer
