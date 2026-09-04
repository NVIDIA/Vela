// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataRoots.h"
#include "ProjectOpDispatcher.h"
#include "ServerOptions.h"
#include "ServerPushDelegate.h"
#include "ServerTaskRunner.h"
#include "ViewportPasses.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "PlaybackMessages.h"
#include "SceneEditMessages.h"
#include "ViewportMessages.h"
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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
 * IO thread and do nothing but decode, then latch into the Control-State
 * Latch or enqueue into one of the two queues that ride alongside it (scene
 * edits, project requests; plus Ping -> Pong); the loop applies all of it
 * once per iteration, so a long-running bootstrap, scene edit or project op
 * never stalls the IO thread and the IO thread never races the renderer.
 * Frames follow the latest-frame-wins, one-in-flight rule: while the
 * previous Frame is still being written the loop skips rendering.
 *
 * Project Ops run through the ProjectOpDispatcher on the loop thread; task
 * ops queue a Server Task that the loop runs to completion, one per
 * iteration, between applying the latch and rendering, so frames pause while
 * a task runs. Every path a request names must lie inside the Data Roots.
 * One rule decides every ProjectSnapshot, in the loop and never in a
 * handler: the ProjectContext counts its mutations (revision()), and the
 * loop sends a snapshot whenever the count differs from the one it last
 * sent -- after each request dispatched, after a task ran, and after the
 * playback tick -- so a reply always precedes its snapshot, a refused
 * or no-op request has none, and a failed op that still left a mark gets
 * one. The pipeline follows the same way: activeShotRevision() moves when
 * which shot renders (or its record) changed, and the loop rebinds on it.
 *
 * Playback free-runs on the loop thread: each iteration applies the latch,
 * ticks the AnimationManager by a steady-clock delta (at most one frame, even
 * while a Frame is still in flight), then renders, so the Frame header names
 * the frame actually rendered (Time in Motion). Time at Rest reaches the
 * replica through snapshots only: SetPlaying is a Project Op (reply plus
 * snapshot), auto-stop at the end of a non-looping shot is a revision the
 * context marks itself, and a SetTime scrub while paused is committed by
 * the loop (markRevised) after 250 ms of quiet. Frames a file binding
 * cannot load go out as TimeAdvanceWarning; playback goes on.
 *
 * The Viewport Pass suite (ViewportPasses) composites the outline, AOV and
 * world-bounds passes over each frame before it is copied out; SetOutline
 * and ViewportSettings are latch slots feeding it. Pick is a latch slot too,
 * one in flight, latest-wins: the loop services it by rendering one frame
 * with the id channel on, even while paused, and answers with a PickReply
 * before the next Frame. RequestArrayHistogram is a sync Project Op.
 *
 * RenderShot is an exclusive Server Task (ProjectOpDispatcher): while it is
 * queued or running, mutating requests are refused with "render in
 * progress"; frames pause because the body holds the loop; a CancelTask
 * naming the running render raises the runner's cancel flag from the IO
 * thread so the body stops at its next frame, as does Shutdown. Scene edits,
 * SetTime and Pick latched while the body ran targeted a scene the render
 * was mutating: they are dropped (the pick with an Error) the moment it
 * returns, before its ending goes out, so a client reacting to the ending
 * loses nothing.
 * The bootstrap replays how tasks ended since the last one (task-status
 * replay) between UIState and the ProjectSnapshot. A second client connecting
 * over a live session replaces it; the replaced client is sent the farewell
 * Disconnect{"replaced by another client"} before its socket closes.
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
  uint16_t port() const;
  SessionState sessionState() const;
  // The ANARI library actually rendering, after any fallback.
  const std::string &libraryName() const;
  // Whether the scene pass renders the objectId channel right now (mirrored
  // from the loop thread for tests).
  bool idChannelEnabled() const;

  // The loop thread's state; other threads may only read it while the server
  // is not rendering (tests).
  vsr::app::Context &appContext();
  ProjectContext &projectContext();

 private:
  using SceneEdit = std::variant<protocol::SetObjectParameter,
      protocol::RemoveObjectParameter,
      protocol::SetNodeTransform>;

  // What the IO thread hands the loop, written under m_controlMutex and
  // swapped out whole once per iteration. Two things live here side by side:
  //
  // - The Control-State Latch proper: one value per input, latest-wins.
  //   Session events name the connection they happened on (its serial, see
  //   m_connectionSerial) so the loop can tell a loss of the old client from
  //   a loss of the one accepted since; frame config, encoding and the
  //   rendering flag simply keep the newest value.
  // - The edit drain queue: scene edits in arrival order, none dropped,
  //   because coalescing them would leave the mirror and the scene
  //   disagreeing. It is a queue, not a latch, and is drained in one go.
  // - The project request queue: decoded project requests in arrival order.
  //   Also a queue; the loop moves it onto m_pendingRequests and dispatches
  //   from there, holding a sync op back while a task the client sent
  //   earlier is still queued so requests take effect in the order sent.
  struct ControlState
  {
    std::optional<uint64_t> accepted;
    std::optional<uint64_t> helloReceived;
    std::optional<uint64_t> disconnected;
    std::string disconnectReason;
    std::optional<uint64_t> closeRequested;
    std::string closeReason;
    vsr::network::MessageFuture farewell; // flushed before the close
    std::optional<protocol::SetFrameConfig> frameConfig;
    std::optional<protocol::FrameEncoding> encoding;
    std::optional<bool> rendering;
    std::vector<SceneEdit> edits;
    std::vector<ProjectRequest> requests;
    // Playback
    std::optional<protocol::SetTime> time; // the scrub, latest-wins
    // Viewport: latest-wins slots feeding the pass suite and the pick
    std::optional<protocol::Pick> pick;
    std::optional<protocol::SetOutline> outline;
    std::optional<protocol::ViewportSettings> viewportSettings;
  };

  // Startup and teardown (caller's thread)
  ProjectOpDispatcher::Host makeDispatcherHost();
  bool loadDevice(std::string *error);
  bool setupProject(std::string *error);
  bool setupRendering(std::string *error);
  bool setupNetwork(std::string *error);
  void teardown();
  // Points the pipeline's renderer and camera at the active shot's, creating
  // the library's standard renderers when the scene has none (a fresh or
  // reopened project) and recording the pick in the shot. Used at setup and
  // whenever the context's activeShotRevision() moved (followProjectRevisions).
  bool bindActiveShotRendering(std::string *error);
  // Forgets the pipeline's renderer and camera: after a project reset the
  // objects they named are gone, so until a bind succeeds no frame renders.
  void unbindRendering();

  // IO thread
  void onConnected();
  void onDisconnected(const boost::system::error_code &error);
  void onMessage(const vsr::network::Message &msg);
  void onHello(const vsr::network::Message &msg);
  void requestClose(
      const std::string &reason, vsr::network::MessageFuture farewell = {});
  void replyError(const std::string &text);
  // Refuses a request the server cannot serve: a ProjectOpReply{ok=false}
  // when the payload carries a non-zero requestId (so the sender can retire
  // it), a bare Error otherwise.
  void refuseRequest(const vsr::network::Message &msg, const std::string &text);

  // Loop thread
  void applyControlState();
  void beginSession(uint64_t serial);
  // closeSocket: the socket is still open (a close the server decided on) and
  // must be shut; false when the peer already closed it, in which case the
  // transport is left alone so a connection accepted since survives.
  void endSession(const std::string &reason, bool closeSocket);
  void bootstrap();
  void sendSceneSnapshot();
  // Drops the edits and SetTime a shot render's body accumulated in the
  // latch and answers a Pick with an Error: they targeted a scene the render
  // was mutating.
  void discardStaleInputs(ControlState &control);
  void dispatchPendingRequests();
  // The one rule for the pipeline and the snapshot, on the context's two
  // revisions: rebinds when activeShotRevision() moved since the last bind,
  // then, with a session up, sends a ProjectSnapshot when revision() moved
  // since the last one sent. Called after each request dispatched, after a
  // task ran and after the playback tick, so every mutation has its own
  // snapshot before the next thing goes out.
  void followProjectRevisions();
  // Sends the Project and records the revision it carried.
  void sendProjectSnapshot();
  void applyFrameConfig(uint32_t width, uint32_t height);
  void applyEdit(const protocol::SetObjectParameter &edit);
  void applyEdit(const protocol::RemoveObjectParameter &edit);
  void applyEdit(const protocol::SetNodeTransform &edit);
  void renderAndSendFrame();
  // Encodes m_colorBytes as this iteration's Frame and sends it.
  void sendRenderedFrame();
  void send(vsr::network::Message &&msg);
  bool sessionEstablished() const;

  // Playback (loop thread)
  // Seeks the active shot to the latched SetTime; other shots are logged and
  // ignored. While paused it opens (or extends) the rest-commit window.
  void applyTime(const protocol::SetTime &time);
  // One tick per iteration; a play->stop flip of the active shot (auto-stop)
  // is a revision the context marks, so the snapshot follows.
  void tickPlayback();
  // Commits Time at Rest (markRevised, so the snapshot follows) once no
  // SetTime has arrived for SCRUB_COMMIT_QUIET and the frame differs from
  // the one the window opened on (a scrub that returns to its start commits
  // nothing).
  void commitScrubIfQuiet();
  // One TimeAdvanceWarning per load failure the manager collected.
  void pushLoadFailures();
  // A client edit on the active shot camera: the manipulator adopts the
  // camera's pose and a keyframe-less camera rig's current view follows, so
  // the next applyActiveShot() writes the client's pose back, not a stale one.
  void followCameraEdit(const vsr::scene::Object *object);
  void setState(SessionState state);
  void setPushEnabled(bool enabled);

  // Viewport (loop thread)
  // Applies the latched ViewportSettings and SetOutline, and takes over a
  // latched Pick as the one pending.
  void applyViewportControl(const ControlState &control);
  // Refreshes the world-bounds pass from the world and the shot camera.
  void prepareViewportPasses();
  // Renders one frame with the id channel on for m_pendingPick, replies, and
  // when rendering sends that frame as well; true when a Frame went out.
  bool servicePendingPick();
  const vsr::scene::Object *shotCameraObject() const;

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

  // Viewport (loop thread; the flag is mirrored for queries)
  ViewportPasses m_viewport;
  std::optional<protocol::Pick> m_pendingPick;
  std::atomic<bool> m_idChannelEnabled{false};

  // Project Ops (loop thread)
  DataRoots m_dataRoots;
  ServerTaskRunner m_tasks;
  ProjectOpDispatcher m_dispatcher;
  std::deque<ProjectRequest> m_pendingRequests;
  protocol::SubtreePtr m_uiState; // null until a project with UI state opens

  std::shared_ptr<vsr::network::NetworkServer> m_server;
  vsr::network::MessageFuture m_frameInFlight;
  bool m_started{false};

  // Session state, loop thread only (m_state is mirrored for queries)
  std::atomic<SessionState> m_state{SessionState::Listening};
  uint64_t m_sessionSerial{0}; // the connection the session runs on
  uint32_t m_frameWidth{0};
  uint32_t m_frameHeight{0};
  protocol::FrameEncoding m_encoding{protocol::FrameEncoding::Raw};
  bool m_renderingRequested{false};
  bool m_bootstrapPending{false};
  bool m_sceneResendPending{false};
  // The context revisions the last snapshot sent and the last bind followed.
  uint64_t m_snapshotRevision{0};
  uint64_t m_boundShotRevision{0};
  // Playback (loop thread only)
  using Clock = std::chrono::steady_clock;
  std::optional<Clock::time_point> m_lastTick;
  bool m_scrubPending{false};
  Clock::time_point m_scrubDeadline{};
  int m_scrubFrameBefore{0}; // the frame time rested on when the window opened

  // Shared with the IO thread
  std::mutex m_controlMutex;
  ControlState m_control;
  std::atomic<bool> m_shutdownRequested{false};

  // IO thread only. The serial counts accepted connections and tags every
  // session event in the latch. The Hello flag is set on a matching Hello and
  // cleared by the next accept (the socket a Hello arrived on is the one every
  // later message shares); the loop never touches it, so a disconnect, accept
  // and Hello applied from one latch batch cannot clear a Hello the IO thread
  // already accepted.
  uint64_t m_connectionSerial{0};
  bool m_helloAccepted{false};
};

} // namespace vsr::scivis_studio::server
