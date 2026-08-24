// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include <vsr/ui/imgui/windows/Window.h>
// vsr_rendering
#include <vsr/rendering/pipeline/ImagePipeline.h>
#include <vsr/rendering/index/RenderIndexAllLayers.hpp>
#include <vsr/rendering/view/Manipulator.hpp>
// std
#include <memory>

namespace vsr::ui::imgui {

struct MultiDeviceViewport : public Window
{
  MultiDeviceViewport(Application *app,
      vsr::rendering::Manipulator *m,
      const char *name = "DP Viewport");
  ~MultiDeviceViewport();

  void buildUI() override;
  void setManipulator(vsr::rendering::Manipulator *m);
  void resetView(bool resetAzEl = true);
  void centerView();

  void setLibrary(const std::string &libName);

 private:
  void loadSettings(vsr::core::DataNode &thisWindowRoot) override;

  void getSceneBounds(vsr::math::float3 bounds[2]) const;
  vsr::rendering::RenderIndexAllLayers *getRenderIndex(size_t i = 0) const;
  void setupImagePipeline(const std::vector<anari::Device> &devices);
  void reshape(vsr::math::int2 newWindowSize);
  void updateCamera(bool force = false);

  void loadANARIRendererParameters();
  void updateAllRendererParameters();

  void ui_menubar();
  void ui_handleInput();

  int windowFlags() const override;
  int pushStyle() override;

  // ImGui input state //

  vsr::math::float2 m_previousMouse{-1.f, -1.f};
  bool m_mouseRotating{false};
  bool m_manipulating{false};

  // rendering //

  anari::DataType m_format{ANARI_UFIXED8_RGBA_SRGB};
  std::vector<anari::Camera> m_cameras;
  std::vector<vsr::rendering::RenderIndexAllLayers *> m_renderIndices;
  vsr::scene::Object m_rendererObject;

  struct RendererUpdateDelegate : public vsr::scene::EmptyUpdateDelegate
  {
    void signalParameterUpdated(
        const vsr::scene::Object *o, const vsr::scene::Parameter *p) override;
    std::vector<anari::Device> devices;
    std::vector<anari::Renderer> renderers;
  } m_rud;

  // camera manipulator //

  int m_arcballUp{1};
  vsr::rendering::Manipulator m_localArcball;
  vsr::rendering::Manipulator *m_arcball{nullptr};
  vsr::rendering::UpdateToken m_cameraToken{0};
  float m_fov{40.f};
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};

  // display //

  bool m_showAxes{true};

  vsr::rendering::ImagePipeline m_pipeline;
  vsr::rendering::MultiDeviceSceneRenderPass *m_anariPass{nullptr};
  vsr::rendering::AnariAxesRenderPass *m_axesPass{nullptr};
  vsr::rendering::CopyToSDLTexturePass *m_outputPass{nullptr};

  vsr::math::int2 m_viewportSize{0, 0};
  vsr::math::int2 m_renderSize{0, 0};
  float m_resolutionScale{1.f};
};

} // namespace vsr::ui::imgui
