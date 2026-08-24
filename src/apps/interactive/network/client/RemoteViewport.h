// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
#include "vsr/ui/imgui/windows/BaseViewport.h"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/view/Manipulator.hpp"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"

#include "../RenderSession.hpp"
// std
#include <mutex>

using vsr::network::MessageType;

namespace vsr::ui::imgui {

struct RemoteViewport : public BaseViewport
{
  RemoteViewport(Application *app,
      vsr::rendering::Manipulator *m,
      vsr::network::NetworkChannel *c,
      const char *name = "Remote Viewport");
  ~RemoteViewport();

  void buildUI() override;
  void setManipulator(vsr::rendering::Manipulator *m);
  void setNetworkChannel(vsr::network::NetworkChannel *c);
  void disconnect();

 private:
  void imagePipeline_populate(vsr::rendering::ImagePipeline &p) override;

  void camera_resetView(bool resetAzEl = true) override;
  void camera_centerView() override;

  void renderer_clone() override;
  void renderer_resetParameterDefaults() override;

  void viewport_reshape(vsr::math::int2 newWindowSize) override;

  void updateRenderer();
  void updateCamera();
  void applyIncomingFrame();

  void ui_menubar();
  void ui_overlay();

  // Data /////////////////////////////////////////////////////////////////////

  bool m_wasConnected{false};
  bool m_showOverlay{true};

  size_t m_receivedRendererIdx{VSR_INVALID_INDEX};
  size_t m_receivedCameraIdx{VSR_INVALID_INDEX};
  vsr::scene::RendererAppRef m_prevRenderer;
  vsr::scene::CameraAppRef m_prevCamera;

  // Camera manipulator //

  bool m_manipulatorSynchronized{false};

  // Networking //

  vsr::network::NetworkChannel *m_channel{nullptr};

  // Display //

  std::mutex m_incomingFrameMutex;
  std::vector<uint8_t> m_incomingColorBuffer;
  std::vector<uint8_t> m_pendingColorBuffer;
  bool m_hasPendingFrame{false};
  vsr::network::RenderSession::Frame::Config m_frameConfig;

  vsr::rendering::ClearBuffersPass *m_clearPass{nullptr};
  vsr::rendering::CopyToColorBufferPass *m_incomingFramePass{nullptr};
  vsr::rendering::CopyToSDLTexturePass *m_outputPass{nullptr};
};

} // namespace vsr::ui::imgui
