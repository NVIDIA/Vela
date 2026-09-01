// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/BaseViewport.h"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/view/Manipulator.hpp"
// vsr_scene
#include "vsr/scene/objects/Camera.hpp"
// std
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * The thin client's viewport. It renders nothing itself: each UI frame it
 * takes the newest Frame the ServerConnection holds, decodes it with the
 * FrameCodec into RGBA8 and presents it through the demo's pass chain
 * (clear -> copy external buffer -> SDL texture). The pipeline follows the
 * frame's own dimensions, so a frame rendered before a resize is shown
 * scaled instead of dropped.
 *
 * Input drives the shared Manipulator, whose changes are written into the
 * Structural Mirror's current camera exactly as the monolith viewport does;
 * the MirrorUpdateDelegate turns those writes into SetObjectParameter. A
 * viewport resize is reported once per settled size as SetFrameConfig.
 *
 * Connection State decides what is shown: Connected presents frames and
 * takes input; Lost keeps the last frame frozen and ignores input;
 * NeverConnected/Disconnected show a "not connected" hint.
 *
 * Example:
 *   auto *viewport = new StudioViewport(this, &ctx->view.manipulator,
 *       &connection, "Viewport");
 *   connection.onBootstrapComplete = [&] {
 *     viewport->sendFrameConfig();
 *     connection.setEncodings(preferred);
 *     connection.startRendering();
 *     viewport->adoptCamera(activeShotCamera);
 *   };
 */
struct StudioViewport : public vsr::ui::imgui::BaseViewport
{
  StudioViewport(vsr::ui::imgui::Application *app,
      vsr::rendering::Manipulator *manipulator,
      ServerConnection *connection,
      const char *name = "Viewport");
  ~StudioViewport() override;

  void buildUI() override;

  // Reports the current viewport size as SetFrameConfig, whatever was last
  // reported; for the bootstrap, where the server starts from scratch.
  void sendFrameConfig();
  // Points the manipulator at `camera`, the mirror's copy of the server's
  // camera. A null camera leaves the viewport without one; input then changes
  // nothing.
  void adoptCamera(vsr::scene::CameraAppRef camera);
  // Lists the mirror's renderers for the Renderer menu, making the one at
  // `rendererIndex` current (the first one when that index is not a
  // renderer). Parameter edits flow out as SetObjectParameter; which renderer
  // the server draws with is not a client choice in this milestone.
  void adoptRenderer(size_t rendererIndex);
  // Back to the home state: no frame, no camera, no renderers.
  void reset();

 private:
  void imagePipeline_populate(vsr::rendering::ImagePipeline &p) override;
  void viewport_reshape(vsr::math::int2 newWindowSize) override;

  void camera_resetView(bool resetAzEl = true) override;
  void camera_centerView() override;
  void renderer_clone() override;
  void renderer_resetParameterDefaults() override;

  void takeLatestFrame();
  void syncPipelineToFrame();
  void updateCamera();
  float receivedFps() const;

  void ui_menubar(bool connected);
  void ui_notConnectedHint();
  void ui_overlay();

  // Data /////////////////////////////////////////////////////////////////////

  using Clock = std::chrono::steady_clock;

  ServerConnection *m_connection{nullptr};
  ConnectionState m_shownState{ConnectionState::NeverConnected};
  bool m_showOverlay{true};

  // Camera //

  bool m_manipulatorSynchronized{false};

  // Frame presentation //

  bool m_hasFrame{false};
  protocol::FrameHeader m_lastHeader;
  std::vector<uint8_t> m_pixels; // what the copy pass reads
  std::vector<uint8_t> m_decodeScratch; // so a failed decode keeps m_pixels
  vsr::math::int2 m_pipelineSize{0, 0};
  vsr::math::uint2 m_sentFrameConfig{0, 0};
  std::deque<Clock::time_point> m_frameArrivals; // last second, for fps

  vsr::rendering::ClearBuffersPass *m_clearPass{nullptr};
  vsr::rendering::CopyToColorBufferPass *m_framePass{nullptr};
  vsr::rendering::CopyToSDLTexturePass *m_outputPass{nullptr};
};

} // namespace vsr::scivis_studio::client
