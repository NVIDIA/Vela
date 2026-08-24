// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <memory>
#include <mutex>
#include <thread>
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_app
#include "vsr/app/Context.h"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/view/ManipulatorToAnari.hpp"

#include "../RenderSession.hpp"

namespace vsr::network {

struct RenderServer
{
  RenderServer(int argc, const char **argv);
  ~RenderServer();

  void run(short port = 12345);

 private:
  enum class ServerMode
  {
    DISCONNECTED,
    PAUSED,
    RENDERING,
    SEND_SCENE,
    SHUTDOWN
  };

  void setup_Scene();
  void setup_ANARIDevice();
  void setup_Camera();
  void setup_ImagePipeline();
  void setup_Messaging();
  void update_FrameConfig();
  void update_View();
  void send_FrameBuffer();
  void set_Mode(ServerMode mode);

  // Data //

  short m_port{12345};

  RenderSession m_session;
  vsr::app::Context m_ctx;

  std::shared_ptr<NetworkServer> m_server;
  MessageFuture m_lastSentFrame;

  std::string m_libName;
  anari::Device m_device{nullptr};
  vsr::scene::CameraAppRef m_camera;
  std::vector<vsr::scene::RendererAppRef> m_renderers;
  vsr::scene::RendererAppRef m_currentRenderer;
  vsr::rendering::RenderIndex *m_renderIndex{nullptr};
  vsr::rendering::ImagePipeline m_renderPipeline;
  vsr::rendering::AnariSceneRenderPass *m_sceneImagePass{nullptr};
  std::mutex m_controlMutex;
  std::mutex m_frameSendMutex;
  int m_viewVersion{0};
  ServerMode m_currentMode{ServerMode::DISCONNECTED};
  ServerMode m_nextMode{ServerMode::DISCONNECTED};
  ServerMode m_previousMode{ServerMode::DISCONNECTED};

  struct SessionVersions
  {
    int frameConfigVersion{-1};
    int viewVersion{-1};
  } m_sessionVersions;
};

} // namespace vsr::network
