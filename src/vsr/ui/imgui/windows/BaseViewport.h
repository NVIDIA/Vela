// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// imgui
#include <imgui.h>
// ImGuizmo
#include <ImGuizmo.h>
// imoguizmo
#include <imoguizmo.hpp>

namespace vsr::ui::imgui {

/*
 * This is a base class for viewports in the VSR UI. It provides common
 * functionality for managing default cameras, renderers, and an image pipeline,
 * as well as a common UI for interacting with these things. It is intended to
 * be subclassed for specific viewport types (eg. a viewport that shows the main
 * scene, a viewport that shows a render preview, etc.) that can customize the
 * UI and rendering as needed.
 */
struct BaseViewport : public Window
{
  BaseViewport(Application *app, const char *name);
  virtual ~BaseViewport() override;

  void buildUI() override;
  void setManipulator(vsr::rendering::Manipulator *m);

 protected:
  void saveSettings(vsr::core::DataNode &thisWindowRoot) override;
  void loadSettings(vsr::core::DataNode &thisWindowRoot) override;

  /////////////////////////////////////////////////////////////////////////////
  // The following represents the internal API for child classes to use common
  // functionality -- methods are organized by theme using a prefix.
  void viewport_setActive(bool active); // global toggle for display/input/etc.
  bool viewport_isActive() const;
  virtual void viewport_reshape(vsr::math::int2 newWindowSize);

  virtual void imagePipeline_populate(vsr::rendering::ImagePipeline &p) = 0;
  void imagePipeline_setup();
  bool imagePipeline_isSetup() const;
  void imagePipeline_setDimensions(uint32_t width, uint32_t height);
  void imagePipeline_render();
  void imagePipeline_teardown();
  const vsr::rendering::ImagePipeline &imagePipeline() const;

  void camera_update(bool force = false);
  void camera_setCurrent(vsr::scene::CameraAppRef c);
  virtual void camera_setUseImplicitAspectRatio(bool on);
  virtual void camera_resetView(bool resetAzEl = true) = 0;
  virtual void camera_centerView() = 0;

  virtual void renderer_clone() = 0;
  virtual void renderer_resetParameterDefaults() = 0;

  bool gizmo_canShow() const;

  void ui_handleInput();
  void ui_gizmo();
  bool ui_orientationWidget(); // returns true if widget consumed mouse input
  void ui_animationSlider();
  void ui_menubar_Renderer();
  void ui_menubar_Camera();
  void ui_menubar_TransformManipulator();
  /////////////////////////////////////////////////////////////////////////////

  struct CameraState
  {
    vsr::scene::CameraAppRef current;
    vsr::rendering::Manipulator localArcball;
    vsr::rendering::Manipulator *arcball{nullptr};
    vsr::rendering::UpdateToken arcballToken{0};
    bool useImplicitAspectRatio{false};
  } m_camera;

  struct ViewportState
  {
    bool active{false};
    vsr::math::int2 size{0, 0};
    vsr::math::int2 renderSize{0, 0};
    vsr::math::int2 pendingSize{0, 0}; // size requested last frame (resize debounce)
    float resolutionScale{1.f};
  } m_viewport;

  struct RendererState
  {
    std::vector<vsr::scene::RendererAppRef> objects;
    vsr::scene::RendererAppRef current;
  } m_renderers;

  bool m_showOrientationWidget{true};
  bool m_showAnimationSlider{true};

 private:
  int windowFlags() const override;
  int pushStyle() override;
  void applyViewMatrixToArcball(const float *viewMat);

  vsr::rendering::ImagePipeline m_pipeline;

  struct InputState
  {
    vsr::math::float2 previousMouse{-1.f, -1.f};
    bool mouseRotating{false};
    bool manipulating{false};
  } m_input;

  struct GizmoState
  {
    bool active{true};
    ImGuizmo::OPERATION operation{ImGuizmo::OPERATION::TRANSLATE};
    ImGuizmo::MODE mode{ImGuizmo::MODE::WORLD};
  } m_gizmo;
};

} // namespace vsr::ui::imgui
