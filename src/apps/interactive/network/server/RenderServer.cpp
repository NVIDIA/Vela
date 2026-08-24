// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RenderServer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
#include "vsr/core/Timer.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
// vsr_rendering
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
// vsr_network
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/ParameterChange.hpp"
#include "vsr/network/messages/ParameterRemove.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferArrayData.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// std
#include <cstdlib>

namespace vsr::network {

RenderServer::RenderServer(int argc, const char **argv)
{
  vsr::core::setLogToStdout(true);
  vsr::core::logStatus("[Server] Parsing command line...");
  m_ctx.parseCommandLine(argc, argv);
}

RenderServer::~RenderServer() = default;

void RenderServer::run(short port)
{
  m_port = port;

  setup_Scene();
  setup_ANARIDevice();
  setup_Camera();
  setup_ImagePipeline();
  setup_Messaging();

  m_server->start();

  vsr::core::logStatus("[Server] Listening on port %i...", int(port));

  while (true) {
    ServerMode nextMode;
    {
      std::lock_guard lock(m_controlMutex);
      if (m_nextMode == ServerMode::SHUTDOWN)
        break;
      nextMode = m_nextMode;
    }

    bool wasRendering = false;
    auto currentMode = [&]() {
      std::lock_guard lock(m_controlMutex);
      wasRendering = m_currentMode == ServerMode::RENDERING;
      m_currentMode =
          m_server->isConnected() ? nextMode : ServerMode::DISCONNECTED;
      return m_currentMode;
    }();

    if (currentMode == ServerMode::DISCONNECTED) {
      if (m_previousMode != ServerMode::DISCONNECTED) {
        vsr::core::logStatus("[Server] Listening on port %i...", int(port));
        m_server->restart();
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else if (currentMode == ServerMode::RENDERING) {
      if (m_previousMode != ServerMode::RENDERING)
        vsr::core::logDebug("[Server] Rendering frames...");
      update_View();
      update_FrameConfig();
      m_renderPipeline.render();
      send_FrameBuffer();
    } else if (currentMode == ServerMode::SEND_SCENE) {
      vsr::core::logStatus("[Server] Serializing + sending scene...");

      vsr::core::Timer timer;
      timer.start();
      vsr::network::messages::TransferScene sceneMsg(&m_ctx.vsr.scene);
      m_server->send(MessageType::CLIENT_RECEIVE_SCENE, std::move(sceneMsg))
          .get();
      const float time = m_ctx.vsr.animationMgr.getAnimationTime();
      m_server->send(MessageType::CLIENT_RECEIVE_TIME, &time).get();
      timer.end();
      vsr::core::logStatus("[Server] ...done! (%.3f s)", timer.seconds());

      set_Mode(wasRendering ? ServerMode::RENDERING : ServerMode::PAUSED);
    } else {
      if (m_previousMode != ServerMode::PAUSED)
        vsr::core::logStatus("[Server] Rendering paused...");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    m_previousMode = currentMode;
  }

  vsr::core::logStatus("[Server] Shutting down...");

  m_server->stop();
  m_server->removeAllHandlers();

  m_camera = {};
  m_ctx.anari.releaseRenderIndex(m_ctx.vsr.scene, m_device);
  m_ctx.anari.releaseAllDevices();
}

void RenderServer::setup_Scene()
{
  vsr::core::logStatus("[Server] Setting up scene from command line...");
  m_ctx.setupSceneFromCommandLine();
  vsr::core::logStatus(
      "%s", vsr::scene::objectDBInfo(m_ctx.vsr.scene.objectDB()).c_str());
  vsr::core::logStatus("[Server] Scene setup complete.");
}

void RenderServer::setup_ANARIDevice()
{
  vsr::core::logStatus("[Server] Loading 'environment' device...");
  const char *libNameEnv = std::getenv("ANARI_LIBRARY");
  if (!libNameEnv) {
    vsr::core::logWarning(
        "[Server] ANARI_LIBRARY environment variable not set,"
        " defaulting to 'helide'");
    libNameEnv = "helide"; // default to helide if env var not set
  } else {
    vsr::core::logStatus(
        "[Server] ANARI_LIBRARY environment variable set to '%s'", libNameEnv);
  }

  m_libName = libNameEnv;

  auto device = m_ctx.anari.loadDevice(m_libName);
  if (!device) {
    vsr::core::logError(
        "[Server] Failed to load '%s' ANARI device.", m_libName.c_str());
    std::exit(EXIT_FAILURE);
  }

  auto &scene = m_ctx.vsr.scene;

  m_device = device;
  m_renderIndex = m_ctx.anari.acquireRenderIndex(scene, m_libName, device);
  m_camera = scene.defaultCamera();
  m_renderers = scene.renderersOfDevice(m_libName).empty()
      ? scene.createStandardRenderers(m_libName, device)
      : scene.renderersOfDevice(m_libName);
  m_currentRenderer = m_renderers[0];
}

void RenderServer::setup_Camera()
{
  vsr::core::logStatus("[Server] Setting up camera...");
  vsr::rendering::Manipulator manipulator;
  manipulator.setConfig(m_renderIndex->computeDefaultView());
  vsr::rendering::updateCameraObject(*m_camera, manipulator, true);
}

void RenderServer::setup_ImagePipeline()
{
  vsr::core::logStatus("[Server] Setting up render pipeline...");

  m_renderPipeline.setDimensions(
      m_session.frame.config.size.x, m_session.frame.config.size.y);

  auto *arp =
      m_renderPipeline.emplace_back<vsr::rendering::AnariSceneRenderPass>(
          m_device);
  arp->setWorld(m_renderIndex->world());
  arp->setRenderer(m_renderIndex->renderer(m_currentRenderer->index()));
  arp->setCamera(m_renderIndex->camera(m_camera->index()));
  arp->setEnableIDs(false);
  m_sceneImagePass = arp;

  auto *ccbp =
      m_renderPipeline.emplace_back<vsr::rendering::CopyFromColorBufferPass>();
  ccbp->setExternalBuffer(m_session.frame.buffers.color);
}

void RenderServer::setup_Messaging()
{
  vsr::core::logStatus("[Server] Setting up messaging...");

  m_server = std::make_shared<NetworkServer>(m_port);

  // Handlers //

  m_server->registerHandler(
      MessageType::ERROR, [](const vsr::network::Message &msg) {
        vsr::core::logError("[Server] Received error from client: '%s'",
            vsr::network::payloadAs<char>(msg));
      });

  m_server->registerHandler(
      MessageType::PING, [](const vsr::network::Message &msg) {
        vsr::core::logStatus("[Server] Received PING from client");
      });

  m_server->registerHandler(
      MessageType::DISCONNECT, [&](const vsr::network::Message &msg) {
        vsr::core::logStatus("[Server] Client signaled disconnection.");
        set_Mode(ServerMode::DISCONNECTED);
      });

  m_server->registerHandler(MessageType::SERVER_START_RENDERING,
      [&](const vsr::network::Message &msg) {
        vsr::core::logStatus(
            "[Server] Starting rendering as requested by client.");
        set_Mode(ServerMode::RENDERING);
      });

  m_server->registerHandler(MessageType::SERVER_STOP_RENDERING,
      [&](const vsr::network::Message &msg) {
        vsr::core::logStatus(
            "[Server] Stopping rendering as requested by client.");
        set_Mode(ServerMode::PAUSED);
        std::lock_guard lock(m_frameSendMutex);
        if (m_lastSentFrame.valid())
          m_lastSentFrame.get();
      });

  m_server->registerHandler(
      MessageType::SERVER_SHUTDOWN, [&](const vsr::network::Message &msg) {
        vsr::core::logStatus("[Server] Shutdown message received from client.");
        set_Mode(ServerMode::SHUTDOWN);
      });

  m_server->registerHandler(MessageType::SERVER_SET_FRAME_CONFIG,
      [&](const vsr::network::Message &msg) {
        RenderSession::Frame::Config config;
        auto pos = 0u;
        if (vsr::network::payloadRead(msg, pos, &config)) {
          int configVersion = 0;
          {
            std::lock_guard lock(m_controlMutex);
            m_session.frame.config = config;
            configVersion = ++m_session.frame.configVersion;
          }
          vsr::core::logDebug(
              "[Server] Received frame config: size=(%u,%u), version=%d",
              config.size.x,
              config.size.y,
              configVersion);
        } else {
          vsr::core::logError(
              "[Server] Invalid payload for SERVER_SET_FRAME_CONFIG");
        }
      });

  m_server->registerHandler(MessageType::SERVER_SET_OBJECT_PARAMETER,
      [this](const vsr::network::Message &msg) {
        vsr::network::messages::ParameterChange paramChange(
            msg, &m_ctx.vsr.scene);
        paramChange.execute();
      });

  m_server->registerHandler(MessageType::SERVER_REMOVE_OBJECT_PARAMETER,
      [this](const vsr::network::Message &msg) {
        vsr::network::messages::ParameterRemove paramRemove(
            msg, &m_ctx.vsr.scene);
        paramRemove.execute();
      });

  m_server->registerHandler(MessageType::SERVER_SET_CURRENT_RENDERER,
      [this](const vsr::network::Message &msg) {
        size_t idx = 0;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &idx)) {
          if (idx < m_renderers.size()) {
            auto renderer = m_renderers[idx];
            vsr::core::logDebug(
                "[Server] Setting current renderer to index %u (subtype '%s')",
                idx,
                renderer->subtype().c_str());
            std::lock_guard lock(m_controlMutex);
            m_currentRenderer = renderer;
            ++m_viewVersion;
          } else {
            vsr::core::logError(
                "[Server] Invalid renderer index %u in "
                "SERVER_SET_CURRENT_RENDERER",
                idx);
          }
        } else {
          vsr::core::logError(
              "[Server] Invalid payload for SERVER_SET_CURRENT_RENDERER");
        }
      });

  m_server->registerHandler(MessageType::SERVER_SET_CURRENT_CAMERA,
      [this](const vsr::network::Message &msg) {
        size_t idx = 0;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &idx)) {
          auto camera = m_ctx.vsr.scene.getObject<vsr::scene::Camera>(idx);
          if (camera) {
            vsr::core::logDebug(
                "[Server] Setting current camera to index %u (subtype '%s')",
                idx,
                camera->subtype().c_str());
            std::lock_guard lock(m_controlMutex);
            m_camera = camera;
            ++m_viewVersion;
          } else {
            vsr::core::logError(
                "[Server] Invalid camera index %u in "
                "SERVER_SET_CURRENT_CAMERA",
                idx);
          }
        } else {
          vsr::core::logError(
              "[Server] Invalid payload for SERVER_SET_CURRENT_CAMERA");
        }
      });

  m_server->registerHandler(MessageType::SERVER_SET_ARRAY_DATA,
      [this](const vsr::network::Message &msg) {
        vsr::network::messages::TransferArrayData arrayData(
            msg, &m_ctx.vsr.scene);
        arrayData.execute();
      });

  m_server->registerHandler(
      MessageType::SERVER_ADD_OBJECT, [this](const vsr::network::Message &msg) {
        vsr::network::messages::NewObject newObj(msg, &m_ctx.vsr.scene);
        newObj.execute();
      });

  m_server->registerHandler(MessageType::SERVER_REMOVE_OBJECT,
      [this](const vsr::network::Message &msg) {
        vsr::network::messages::RemoveObject removeObj(msg, &m_ctx.vsr.scene);
        removeObj.execute();
      });

  m_server->registerHandler(MessageType::SERVER_REMOVE_ALL_OBJECTS,
      [this](const vsr::network::Message &) {
        m_ctx.vsr.scene.removeAllObjects();
      });

  m_server->registerHandler(MessageType::SERVER_UPDATE_LAYER,
      [this](const vsr::network::Message &msg) {
        vsr::network::messages::TransferLayer layerMsg(msg, &m_ctx.vsr.scene);
        layerMsg.execute();
      });

  m_server->registerHandler(MessageType::SERVER_UPDATE_TIME,
      [this](const vsr::network::Message &msg) {
        float time = 0.f;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &time)) {
          m_ctx.vsr.animationMgr.setAnimationTime(time);
        } else {
          vsr::core::logError(
              "[Server] Invalid payload for SERVER_UPDATE_TIME");
        }
      });

  m_server->registerHandler(MessageType::SERVER_SAVE_STATE_FILE,
      [this](const vsr::network::Message &msg) {
        std::string filename;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, filename)) {
          vsr::core::logStatus(
              "[Server] Saving Scene Archive '%s' as requested by client.",
              filename.c_str());
          if (!vsr::io::save_SceneArchive(
                  m_ctx.vsr.scene, filename.c_str())) {
            vsr::core::logError(
                "[Server] Failed to save Scene Archive '%s'", filename.c_str());
          }
        } else {
          vsr::core::logError(
              "[Server] Invalid payload for SERVER_SAVE_STATE_FILE");
        }
      });

  m_server->registerHandler(MessageType::SERVER_REQUEST_FRAME_CONFIG,
      [this, s = m_server](const vsr::network::Message &msg) {
        vsr::core::logDebug("[Server] Client requested frame config.");
        RenderSession::Frame::Config config;
        {
          std::lock_guard lock(m_controlMutex);
          config = m_session.frame.config;
        }
        s->send(MessageType::CLIENT_RECEIVE_FRAME_CONFIG, &config);
      });

  m_server->registerHandler(MessageType::SERVER_REQUEST_CURRENT_RENDERER,
      [this, s = m_server](const vsr::network::Message &msg) {
        vsr::core::logDebug("[Server] Client requested current renderer.");
        size_t idx = 0;
        {
          std::lock_guard lock(m_controlMutex);
          idx = m_currentRenderer->index();
        }
        s->send(MessageType::CLIENT_RECEIVE_CURRENT_RENDERER, &idx);
      });

  m_server->registerHandler(MessageType::SERVER_REQUEST_CURRENT_CAMERA,
      [this, s = m_server](const vsr::network::Message &msg) {
        vsr::core::logDebug("[Server] Client requested current camera.");
        size_t idx = 0;
        {
          std::lock_guard lock(m_controlMutex);
          idx = m_camera->index();
        }
        s->send(MessageType::CLIENT_RECEIVE_CURRENT_CAMERA, &idx);
      });

  m_server->registerHandler(MessageType::SERVER_REQUEST_SCENE,
      [this](const vsr::network::Message &msg) {
        vsr::core::logDebug("[Server] Client requested scene...");
        // Notify client a big message is coming...
        m_server->send(MessageType::CLIENT_SCENE_TRANSFER_BEGIN);
        set_Mode(ServerMode::SEND_SCENE);
      });
}

void RenderServer::update_View()
{
  vsr::scene::RendererAppRef renderer;
  vsr::scene::CameraAppRef camera;
  int viewVersion = 0;

  {
    std::lock_guard lock(m_controlMutex);
    if (m_viewVersion == m_sessionVersions.viewVersion)
      return;

    renderer = m_currentRenderer;
    camera = m_camera;
    viewVersion = m_viewVersion;
  }

  m_sceneImagePass->setRenderer(m_renderIndex->renderer(renderer->index()));
  m_sceneImagePass->setCamera(m_renderIndex->camera(camera->index()));
  m_sessionVersions.viewVersion = viewVersion;
}

void RenderServer::update_FrameConfig()
{
  RenderSession::Frame::Config config;
  vsr::scene::CameraAppRef camera;
  int configVersion = 0;
  {
    std::lock_guard lock(m_controlMutex);
    if (m_session.frame.configVersion == m_sessionVersions.frameConfigVersion)
      return;

    config = m_session.frame.config;
    camera = m_camera;
    configVersion = m_session.frame.configVersion;
  }

  m_renderPipeline.setDimensions(config.size.x, config.size.y);
  m_sessionVersions.frameConfigVersion = configVersion;

  auto d = m_device;
  auto c = m_renderIndex->camera(camera->index());
  anari::setParameter(
      d, c, "aspect", float(config.size.x) / float(config.size.y));
  anari::commitParameters(d, c);
}

void RenderServer::send_FrameBuffer()
{
  std::lock_guard lock(m_frameSendMutex);
  if (!is_ready<boost::system::error_code>(m_lastSentFrame)) {
    vsr::core::logStatus(
        "[Server] Previous frame still being sent, skipping this frame.");
    return;
  }

  m_lastSentFrame =
      m_server->send(MessageType::CLIENT_RECEIVE_FRAME_BUFFER_COLOR,
          m_session.frame.buffers.color);
}

void RenderServer::set_Mode(ServerMode mode)
{
  std::lock_guard lock(m_controlMutex);
  const bool shuttingDown = m_nextMode == ServerMode::SHUTDOWN
      || m_currentMode == ServerMode::SHUTDOWN;
  if (shuttingDown) // if shutting down, do not change mode
    return;
  m_nextMode = mode;
}

} // namespace vsr::network
