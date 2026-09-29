// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseViewport.h"

// vsr_core
#include "vsr/scene/objects/Camera.hpp"
// vsr_rendering
#include "vsr/rendering/index/RenderIndex.hpp"
#include "vsr/rendering/overlay/WorldBoundsOverlay.h"
#include "vsr/rendering/pick/PickRequest.h"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/passes/AnariSceneRenderPass.h"
#include "vsr/rendering/pipeline/passes/AutoExposurePass.h"
#include "vsr/rendering/pipeline/passes/CopyToSDLTexturePass.h"
#include "vsr/rendering/pipeline/passes/OutlineRenderPass.h"
#include "vsr/rendering/pipeline/passes/OutputTransformPass.h"
#include "vsr/rendering/pipeline/passes/PrimitiveOutlineRenderPass.h"
#include "vsr/rendering/pipeline/passes/ToneMapPass.h"
#include "vsr/rendering/pipeline/passes/VisualizeAOVPass.h"
#include "vsr/rendering/pipeline/saveImage.h"
#include "vsr/rendering/view/Manipulator.hpp"
// anari
#include <anari/frontend/anari_enums.h>
// std
#include <functional>
#include <limits>
#include <optional>
#include <string>

namespace vsr::ui::imgui {

using ViewportDeviceChangeCb = std::function<void(const std::string &)>;

struct Viewport : public BaseViewport
{
  Viewport(Application *app,
      vsr::rendering::Manipulator *m,
      const char *name = "Viewport");
  ~Viewport();

  void buildUI() override;
  void setLibrary(const std::string &libName,
      size_t rendererIndex = VSR_INVALID_INDEX,
      bool resetInitialView = true);
  void setLibraryToDefault();
  const std::string &libraryName() const;
  size_t currentRendererObjectIndex() const;
  void setDeviceChangeCb(ViewportDeviceChangeCb cb);
  void setExternalInstances(
      const anari::Instance *instances = nullptr, size_t count = 0);
  void setCustomFrameParameter(const char *name, const vsr::core::Any &value);
  void setRenderingEnabled(bool enabled);
  void releaseSceneReferences();

 protected:
  void saveSettings(vsr::core::DataNode &thisWindowRoot) override;
  void loadSettings(vsr::core::DataNode &thisWindowRoot) override;
  void saveSceneSettings(vsr::core::DataNode &thisWindowRoot) override;
  void loadSceneSettings(vsr::core::DataNode &thisWindowRoot) override;

 private:
  void refreshCurrentDevice();

  void imagePipeline_populate(vsr::rendering::ImagePipeline &p) override;

  void camera_setUseImplicitAspectRatio(bool on) override;
  void camera_resetView(bool resetAzEl = true) override;
  void camera_centerView() override;

  void renderer_clone() override;
  void renderer_resetParameterDefaults() override;

  void teardownDevice();
  // 'pixel' in render-image pixels, row 0 at the bottom.
  void pick(vsr::math::uint2 pixel, bool selectObject);
  void setSelectionVisibilityFilterEnabled(bool enabled);

  void updateFrame();
  void updateImage();
  void updateWorldBoundsOverlay();
  void syncImagePassState();
  bool sourceSupports(vsr::rendering::ImageChannels channels) const;
  void updateDisplayPassState();

  void ui_menubar();
  void ui_menubar_Device();
  void ui_menubar_Camera();
  void ui_menubar_Viewport();
  void ui_menubar_World();

  bool ui_picking();
  std::optional<vsr::math::uint2> imagePixelUnderMouse() const;
  void ui_overlay();

  // Data /////////////////////////////////////////////////////////////////////

  size_t m_defragToken{0};

  ViewportDeviceChangeCb m_deviceChangeCb;
  float m_timeToLoadDevice{0.f};
  std::string m_libName;
  vsr::rendering::RenderIndex *m_rIdx{nullptr};
  vsr::app::RenderIndexKind m_lastIndexKind{
      vsr::app::RenderIndexKind::ALL_LAYERS};

  bool m_showOverlay{true};
  bool m_renderingEnabled{true};
  bool m_highlightSelection{true};
  bool m_outlinePrimitives{false};
  bool m_showOnlySelected{false};
  bool m_showWorldBounds{false};
  vsr::math::float4 m_worldBoundsColor{0.8f, 0.8f, 0.8f, 1.f};
  int m_worldBoundsWidth{1};
  std::optional<float> m_frameProgress{0.f};

  vsr::rendering::AOVType m_visualizeAOV{vsr::rendering::AOVType::NONE};
  float m_depthVisualMinimum{0.f};
  float m_depthVisualMaximum{1.f};
  bool m_edgeInvert{false};
  anari::DataType m_colorFormat{ANARI_UFIXED8_RGBA_SRGB};

  vsr::rendering::ToneMapOperator m_toneMapOperator{
      vsr::rendering::ToneMapOperator::ACES};
  bool m_autoExposureEnabled{false};
  float m_toneMapExposure{0.f};
  float m_toneMapGamma{2.2f};
  float m_currentAutoExposure{0.f};

  // Where the render image was last drawn, in ImGui screen coordinates.
  vsr::math::float2 m_imageRectMin{0.f, 0.f};
  vsr::math::float2 m_imageRectMax{0.f, 0.f};

  // ANARI objects //

  anari::Device m_device{nullptr};
  vsr::scene::RendererAppRef m_prevRenderer;
  vsr::scene::CameraAppRef m_prevCamera;
  vsr::core::ObjectVersion m_lastCameraChange{};

  // Display //

  vsr::rendering::AnariSceneRenderPass *m_anariPass{nullptr};
  vsr::rendering::VisualizeAOVPass *m_visualizeAOVPass{nullptr};
  vsr::rendering::AutoExposurePass *m_autoExposurePass{nullptr};
  vsr::rendering::ToneMapPass *m_toneMapPass{nullptr};
  vsr::rendering::OutputTransformPass *m_outputTransformPass{nullptr};
  vsr::rendering::PrimitiveOutlineRenderPass *m_primitiveOutlinePass{nullptr};
  vsr::rendering::OutlineRenderPass *m_outlinePass{nullptr};
  std::optional<vsr::rendering::WorldBoundsOverlay> m_worldBounds;
  vsr::rendering::CopyToSDLTexturePass *m_outputPass{nullptr};

  float m_latestFL{0.f};
  float m_latestAnariFL{0.f};
  std::optional<float> m_minFL;
  std::optional<float> m_maxFL;
};

} // namespace vsr::ui::imgui
