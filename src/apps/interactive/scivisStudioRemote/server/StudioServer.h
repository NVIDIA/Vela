// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ServerOptions.h"
#include "ServerPushDelegate.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "SceneEditMessages.h"
// vsr_scivis_studio_model
#include "ProjectContext.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_app
#include "vsr/app/Context.h"
// vsr_rendering
#include "vsr/rendering/index/RenderIndex.hpp"
#include "vsr/rendering/pipeline/ImagePipeline.h"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vsr::scivis_studio::server {

// Where the single client session stands. Listening -> AwaitingHello on
// accept; Connected once the Hellos match and the Bootstrap is out;
// Connected <-> Rendering on StartRendering/StopRendering; any loss returns
// to Listening. Shutdown is terminal.
enum class SessionState
{
  Listening,
  AwaitingHello,
  Connected,
  Rendering,
  Shutdown
};

const char *toString(SessionState state);

/*
 * The headless SciVis Studio server at remote-viewer parity: it owns the
 * vsr::app::Context, the ProjectContext, one ANARI device with its
 * RenderIndex and ImagePipeline, and the NetworkServer, and streams frames of
 * the active shot to one client at a time.
 *
 * Threading: run() is the render loop and the only thread that touches the
 * Scene, the Project or the pipeline. Network handlers run on the channel's
 * IO thread and do nothing but decode and latch into the Control-State Latch
 * (plus Ping -> Pong); the loop applies the latch once per iteration, so a
 * long-running bootstrap or scene edit never stalls the IO thread and the
 * IO thread never races the renderer. Frames follow the latest-frame-wins,
 * one-in-flight rule: while the previous Frame is still being written the
 * loop skips rendering.
 *
 * Milestone 3 answers Project Ops, Server Tasks, Remote Browse, playback,
 * picking and RenderShot with Error{"... not implemented in this server"}.
 *
 * Example:
 *   StudioServer server(options);
 *   std::string error;
 *   if (!server.start(&error))
 *     return fail(error);
 *   server.run(); // until Shutdown arrives or requestShutdown() is called
 */
struct StudioServer
{
  explicit StudioServer(const ServerOptions &options);
  ~StudioServer();

  VSR_NOT_COPYABLE(StudioServer)
  VSR_NOT_MOVEABLE(StudioServer)

  // Loads the ANARI device (falling back through the library list), opens
  // --project or creates an unsaved one, builds the render pipeline for the
  // active shot and binds the port. False with the reason on failure, after
  // which run() must not be called.
  bool start(std::string *error = nullptr);

  // The render loop; returns once a Shutdown message arrives or
  // requestShutdown() was called, after releasing every resource.
  void run();

  // Thread- and signal-safe; the loop exits at its next iteration.
  void requestShutdown();

  // Queries (any thread) //

  // The port actually bound; meaningful after start().
  unsigned short port() const;
  SessionState sessionState() const;
  // The ANARI library actually rendering, after any fallback.
  const std::string &libraryName() const;

  // The loop thread's state; other threads may only read it while the server
  // is not rendering (tests).
  vsr::app::Context &appContext();
  ProjectContext &projectContext();

 private:
  using SceneEdit = std::variant<protocol::SetObjectParameter,
      protocol::RemoveObjectParameter,
      protocol::SetNodeTransform>;

  // The Control-State Latch: written by the IO thread under m_controlMutex,
  // swapped out and applied by the loop once per iteration. Session events
  // are flags; interactive values are latest-wins; scene edits are an ordered
  // drain queue because dropping one would leave the mirror and the scene
  // disagreeing.
  struct ControlState
  {
    bool accepted{false};
    bool helloReceived{false};
    bool disconnected{false};
    std::string disconnectReason;
    bool closeRequested{false};
    std::string closeReason;
    vsr::network::MessageFuture farewell; // flushed before the close
    std::optional<protocol::SetFrameConfig> frameConfig;
    std::optional<protocol::FrameEncoding> encoding;
    std::optional<bool> rendering;
    std::vector<SceneEdit> edits;
  };

  // Startup and teardown (caller's thread)
  bool loadDevice(std::string *error);
  bool setupProject(std::string *error);
  bool setupRendering(std::string *error);
  bool setupNetwork(std::string *error);
  void teardown();

  // IO thread
  void onConnected();
  void onDisconnected(const boost::system::error_code &error);
  void onMessage(const vsr::network::Message &msg);
  void onHello(const vsr::network::Message &msg);
  void replyError(const std::string &text);

  // Loop thread
  void applyControlState();
  void beginSession();
  void endSession(const std::string &reason);
  void bootstrap();
  void sendSceneSnapshot();
  void applyFrameConfig(uint32_t width, uint32_t height);
  void applyEdit(const protocol::SetObjectParameter &edit);
  void applyEdit(const protocol::RemoveObjectParameter &edit);
  void applyEdit(const protocol::SetNodeTransform &edit);
  void renderAndSendFrame();
  void send(vsr::network::Message &&msg);
  void setState(SessionState state);
  void setPushEnabled(bool enabled);

  ServerOptions m_options;
  vsr::app::Context m_ctx; // outlives m_projectContext: declared first
  ProjectContext m_projectContext;

  std::string m_libraryName;
  anari::Device m_device{nullptr};
  vsr::rendering::RenderIndex *m_renderIndex{nullptr};
  std::vector<vsr::scene::RendererAppRef> m_renderers;
  vsr::scene::RendererAppRef m_renderer;
  size_t m_cameraIndex{VSR_INVALID_INDEX};
  vsr::rendering::ImagePipeline m_pipeline;
  vsr::rendering::AnariSceneRenderPass *m_scenePass{nullptr};
  std::vector<uint8_t> m_colorBytes; // RGBA8, filled by the pipeline
  std::vector<std::byte> m_encodedPixels;
  ServerPushDelegate *m_push{nullptr};

  std::shared_ptr<vsr::network::NetworkServer> m_server;
  vsr::network::MessageFuture m_frameInFlight;
  bool m_started{false};

  // Session state, loop thread only (m_state is mirrored for queries)
  std::atomic<SessionState> m_state{SessionState::Listening};
  uint32_t m_frameWidth{0};
  uint32_t m_frameHeight{0};
  protocol::FrameEncoding m_encoding{protocol::FrameEncoding::Raw};
  bool m_renderingRequested{false};
  bool m_bootstrapPending{false};
  bool m_sceneResendPending{false};

  // Shared with the IO thread
  std::mutex m_controlMutex;
  ControlState m_control;
  std::atomic<bool> m_shutdownRequested{false};
  // Set by the IO thread on a matching Hello, cleared when the session ends;
  // gates every message that needs an established session.
  std::atomic<bool> m_helloAccepted{false};
};

} // namespace vsr::scivis_studio::server
