// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataRoots.h"
#include "Playback.h"
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
// accept; Bootstrapping once the Hellos match, for the one loop iteration
// that sends the Bootstrap; Established from BootstrapEnd on, whether or not
// the client asked for frames (streaming() says); any loss returns to
// Listening. Shutdown is terminal.
enum class SessionState
{
  Listening,
  AwaitingHello,
  Bootstrapping,
  Established,
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
 * Playback (the Playback member) free-runs on the loop thread: each
 * iteration applies the latch, ticks the AnimationManager by a steady-clock
 * delta (at most one frame, even while a Frame is still in flight), then
 * renders, so the Frame header names the frame actually rendered (Time in
 * Motion). Time at Rest reaches the replica through snapshots only:
 * SetPlaying is a Project Op (reply plus snapshot), auto-stop at the end of
 * a non-looping shot is a revision the context marks itself, and a SetTime
 * scrub while paused is committed (markRevised) after 250 ms of quiet.
 * Frames a file binding cannot load go out as TimeAdvanceWarning; playback
 * goes on.
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
 * thread so the body stops at its next frame, as does a shutdown (the
 * runner's stopAll). Scene edits, SetTime and Pick latched while the body
 * ran targeted a scene the render was mutating: the runner hands the
 * ending back unsent and the loop drops them (the pick with an Error)
 * before sending it (runOneTask), however the body left -- the last frame,
 * a cancel, or a throw -- so a client reacting to the ending loses nothing.
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
  // Whether the Established session asked for frames (StartRendering); false
  // without a session.
  bool streaming() const;
  // The ANARI library actually rendering, after any fallback.
  const std::string &libraryName() const;

  // The loop thread's state; other threads may only read it while the server
  // is not rendering (tests).
  vsr::app::Context &appContext();
  ProjectContext &projectContext();
  const ViewportPasses &viewport() const;

 private:
  using SceneEdit = std::variant<protocol::SetObjectParameter,
      protocol::RemoveObjectParameter,
      protocol::SetNodeTransform>;

  // Something that happened to a connection on the IO thread, tagged with
  // that connection's serial (see m_connectionSerial) so the loop can tell a
  // loss of the old client from a loss of the one accepted since.
  struct SessionEvent
  {
    enum class Kind
    {
      Accepted, // a connection was accepted and sent its Hello
      Hello, // its Hello matched ours
      Lost, // the peer closed it
      CloseRequested // the server decided to close it
    };
    Kind kind;
    uint64_t serial;
    std::string reason; // Lost, CloseRequested
    vsr::network::MessageFuture farewell; // CloseRequested: flushed first
  };

  // What the IO thread hands the loop, written under m_controlMutex and
  // swapped out whole once per iteration. Four things live here side by
  // side:
  //
  // - The session event queue: in arrival order, none dropped, so the loop
  //   replays what happened to the connections in the order it happened.
  // - The Control-State Latch proper: one value per input, latest-wins;
  //   frame config, encoding and the rendering flag simply keep the newest
  //   value.
  // - The edit drain queue: scene edits in arrival order, none dropped,
  //   because coalescing them would leave the mirror and the scene
  //   disagreeing. It is a queue, not a latch, and is drained in one go.
  // - The project request queue: decoded project requests in arrival order.
  //   Also a queue; the loop moves it onto the session's pendingRequests and
  //   dispatches from there, holding a sync op back while a task the client
  //   sent earlier is still queued so requests take effect in the order sent.
  struct ControlState
  {
    std::vector<SessionEvent> events;
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

  // What belongs to the one client session and goes with it: reset whole
  // (m_session = {}) when a session begins or ends. Loop thread only; the
  // two facts other threads query, the state and the streaming flag, are the
  // atomics beside it.
  struct Session
  {
    uint64_t serial{0}; // the connection the session runs on
    protocol::FrameEncoding encoding{protocol::FrameEncoding::Raw};
    // The push delegate asked for the structural scene to be resent
    // (an update it cannot express as a push); the loop does so next.
    bool sceneResendPending{false};
    // The Frame on the wire: latest-frame-wins, one in flight.
    vsr::network::MessageFuture frameInFlight;
    std::deque<ProjectRequest> pendingRequests;
    std::optional<protocol::Pick> pendingPick; // one in flight, latest-wins
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
  void latchSessionEvent(SessionEvent event);
  void requestClose(
      const std::string &reason, vsr::network::MessageFuture farewell = {});
  void replyError(const std::string &text);
  // Refuses a request the server cannot serve: a ProjectOpReply{ok=false}
  // when the payload carries a non-zero requestId (so the sender can retire
  // it), a bare Error otherwise.
  void refuseRequest(const vsr::network::Message &msg, const std::string &text);

  // Loop thread
  void applyControlState();
  // One session event, in order: a loss of or a close request for the session
  // in progress ends it, an accept opens the next one, a Hello on it starts
  // the Bootstrap; an event for any other connection is stale and dropped.
  // socketGone: the close request's socket is closed already (see
  // socketClosedInBatch), so the loss or accept that follows ends the
  // session and the close is dropped too.
  void applySessionEvent(SessionEvent &event, bool socketGone);
  // Whether a batch closes the socket a close request names before the loop
  // could: the transport holds one socket, so a later accept replaced it, and
  // a loss of the same connection means the peer closed it.
  static bool socketClosedInBatch(
      const SessionEvent &close, const std::vector<SessionEvent> &events);
  void beginSession(uint64_t serial);
  // closeSocket: the socket is still open (a close the server decided on) and
  // must be shut; false when the peer already closed it, in which case the
  // transport is left alone so a connection accepted since survives.
  void endSession(const std::string &reason, bool closeSocket);
  // What a session's start and end share: the Session value and everything
  // that follows the client (streaming, pushes, queued tasks, scrub window).
  void resetSession();
  void bootstrap();
  void sendSceneSnapshot();
  // Runs one queued Server Task and sends its ending; after the exclusive
  // one (the shot render) the latch is discarded first.
  void runOneTask();
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

  // A client edit on the active shot camera: the manipulator adopts the
  // camera's pose and a keyframe-less camera rig's current view follows, so
  // the next applyActiveShot() writes the client's pose back, not a stale one.
  void followCameraEdit(const vsr::scene::Object *object);
  void setState(SessionState state);
  void setStreaming(bool streaming);
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

  // Viewport (loop thread)
  ViewportPasses m_viewport;

  // Project Ops (loop thread)
  DataRoots m_dataRoots;
  ServerTaskRunner m_tasks;
  ProjectOpDispatcher m_dispatcher;
  protocol::SubtreePtr m_uiState; // null until a project with UI state opens

  std::shared_ptr<vsr::network::NetworkServer> m_server;
  bool m_started{false};

  // Session state, written on the loop thread only; the state and the
  // streaming flag are the two facts other threads query, so they are atomic.
  std::atomic<SessionState> m_state{SessionState::Listening};
  std::atomic<bool> m_streaming{false};
  Session m_session;
  // The frame size outlives a session: the next bootstrap announces it.
  uint32_t m_frameWidth{0};
  uint32_t m_frameHeight{0};
  // The context revisions the last snapshot sent and the last bind followed.
  // Server state, not session state: the bind is the pipeline's, and the
  // bootstrap's snapshot records where a new client stands regardless.
  uint64_t m_snapshotRevision{0};
  uint64_t m_boundShotRevision{0};
  // Playback (loop thread only)
  Playback m_playback;

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
