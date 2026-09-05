// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "ProjectSnapshot.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "CameraRig.h"
// vsr_network
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// vsr_rendering
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Camera.hpp"
#include "vsr/scene/objects/Renderer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <chrono>
#include <iterator>
#include <thread>

namespace vsr::scivis_studio::server {

using namespace protocol;
using vsr::network::Message;
namespace messages = vsr::network::messages;

namespace {

// A refused Hello still deserves its Error: bound on flushing it before the
// socket closes.
constexpr std::chrono::milliseconds FAREWELL_TIMEOUT{200};
// Loop pacing when there is nothing to render.
constexpr std::chrono::milliseconds LISTENING_SLEEP{20};
constexpr std::chrono::milliseconds PAUSED_SLEEP{1};
// Guards the pipeline against a hostile or confused SetFrameConfig.
constexpr uint32_t MAX_FRAME_DIMENSION = 16384;

// Same fallback RenderShot uses: the requested library first, then the rest
// of the device manager's list. `libName` ends up naming what was loaded.
anari::Device loadFirstAvailableDevice(
    vsr::app::ANARIDeviceManager &deviceManager, std::string &libName)
{
  if (auto device = deviceManager.loadDevice(libName))
    return device;

  if (!libName.empty() && libName != "{none}") {
    vsr::core::logWarning(
        "[StudioServer] failed to load ANARI device '%s'; trying the other"
        " libraries",
        libName.c_str());
  }

  for (const auto &fallback : deviceManager.libraryList()) {
    if (fallback == libName)
      continue;
    if (auto device = deviceManager.loadDevice(fallback)) {
      libName = fallback;
      return device;
    }
  }

  libName.clear();
  return nullptr;
}

} // namespace

const char *toString(SessionState state)
{
  switch (state) {
  case SessionState::Listening:
    return "Listening";
  case SessionState::AwaitingHello:
    return "AwaitingHello";
  case SessionState::Bootstrapping:
    return "Bootstrapping";
  case SessionState::Established:
    return "Established";
  case SessionState::Shutdown:
    return "Shutdown";
  }
  return "Unknown";
}

// Construction ///////////////////////////////////////////////////////////////

StudioServer::StudioServer(const ServerOptions &options)
    : m_options(options),
      m_projectContext(&m_ctx),
      m_playback(m_ctx,
          m_projectContext,
          [this](Message &&msg) { send(std::move(msg)); }),
      m_tasks([this](Message &&msg) { send(std::move(msg)); }),
      m_dispatcher(makeDispatcherHost())
{}

ProjectOpDispatcher::Host StudioServer::makeDispatcherHost()
{
  ProjectOpDispatcher::Host host;
  host.projectContext = &m_projectContext;
  host.dataRoots = &m_dataRoots;
  host.tasks = &m_tasks;
  host.send = [this](Message &&msg) { send(std::move(msg)); };
  host.flushScenePushes = [this] {
    if (m_session.sceneResendPending)
      sendSceneSnapshot();
  };
  host.uiState = &m_uiState;
  return host;
}

StudioServer::~StudioServer()
{
  teardown();
}

// Queries ////////////////////////////////////////////////////////////////////

uint16_t StudioServer::port() const
{
  return m_server ? m_server->port() : 0;
}

SessionState StudioServer::sessionState() const
{
  return m_state.load();
}

bool StudioServer::streaming() const
{
  return m_streaming.load();
}

const std::string &StudioServer::libraryName() const
{
  return m_libraryName;
}

vsr::app::Context &StudioServer::appContext()
{
  return m_ctx;
}

ProjectContext &StudioServer::projectContext()
{
  return m_projectContext;
}

const ViewportPasses &StudioServer::viewport() const
{
  return m_viewport;
}

// Startup ////////////////////////////////////////////////////////////////////

bool StudioServer::start(std::string *error)
{
  if (m_started) {
    if (error)
      *error = "server already started";
    return false;
  }

  m_dataRoots = DataRoots(m_options.dataRoots, m_options.projectDirectory);
  for (const auto &root : m_dataRoots.roots()) {
    vsr::core::logStatus("[StudioServer] Data Root: %s", root.string().c_str());
  }

  if (!loadDevice(error) || !setupProject(error) || !setupRendering(error)
      || !setupNetwork(error)) {
    teardown();
    return false;
  }

  m_started = true;
  return true;
}

bool StudioServer::loadDevice(std::string *error)
{
  auto &deviceManager = m_ctx.anari;

  std::string requested = m_options.library;
  if (requested.empty()) {
    // The monolith viewport's default: the first loadable list entry.
    for (const auto &name : deviceManager.libraryList()) {
      if (deviceManager.isLoadableLibrary(name)) {
        requested = name;
        break;
      }
    }
  }

  m_libraryName = requested;
  m_device = loadFirstAvailableDevice(deviceManager, m_libraryName);
  if (!m_device) {
    if (error) {
      *error =
          "no ANARI device could be loaded (requested '" + requested + "')";
    }
    return false;
  }

  if (m_libraryName != requested) {
    vsr::core::logWarning(
        "[StudioServer] rendering with ANARI library '%s' instead of '%s'",
        m_libraryName.c_str(),
        requested.c_str());
  } else {
    vsr::core::logStatus(
        "[StudioServer] ANARI library '%s' loaded", m_libraryName.c_str());
  }
  return true;
}

bool StudioServer::setupProject(std::string *error)
{
  if (!m_options.projectDirectory.empty()) {
    // The same tree the dispatcher keeps after an OpenProject: the
    // bootstrap hands it to the client and a save without one writes it
    // back.
    auto uiState = makeSubtree();
    std::string openError;
    if (!m_projectContext.openProject(
            m_options.projectDirectory, &uiState->root(), &openError)) {
      if (error) {
        *error = "failed to open project '"
            + m_options.projectDirectory.string() + "': " + openError;
      }
      return false;
    }
    m_uiState = uiState;
    vsr::core::logStatus("[StudioServer] opened project '%s' (%s)",
        m_projectContext.project().name.c_str(),
        m_options.projectDirectory.c_str());
  } else {
    // What the monolith does on launch without a directory: a project with
    // one active shot, its camera, a default light rig and camera rig.
    m_projectContext.createUnsavedProject();
    vsr::core::logStatus("[StudioServer] started on a new unsaved project");
  }

  if (!project::activeShot(m_projectContext.project())) {
    if (error)
      *error = "project has no active shot to render";
    return false;
  }

  m_projectContext.applyActiveShot();
  return true;
}

bool StudioServer::bindActiveShotRendering(std::string *error)
{
  auto &scene = m_ctx.vsr.scene;
  auto &project = m_projectContext.project();
  auto *shot = project::activeShot(project);
  if (!shot) {
    unbindRendering();
    if (error)
      *error = "project has no active shot to render";
    return false;
  }

  // A fresh project has no renderer objects at all; like the monolith's
  // viewport and ShotEditor, the server creates the standard set for its
  // library and records its pick in the shot so RenderShot agrees with it.
  auto &settings = shot->renderSettings;
  m_renderers = scene.renderersOfDevice(m_libraryName);
  if (m_renderers.empty())
    m_renderers = scene.createStandardRenderers(m_libraryName, m_device);
  if (m_renderers.empty()) {
    unbindRendering();
    if (error)
      *error = "ANARI library '" + m_libraryName + "' offers no renderers";
    return false;
  }

  vsr::scene::RendererAppRef renderer;
  if (settings.rendererObjectIndex != VSR_INVALID_INDEX) {
    auto candidate =
        scene.getObject<vsr::scene::Renderer>(settings.rendererObjectIndex);
    if (candidate && candidate->rendererDeviceName() == m_libraryName)
      renderer = candidate;
  }
  if (!renderer) {
    renderer = m_renderers.front();
    // Filling in a shot that never picked a renderer object (a fresh
    // project's, or one saved before any pick) completes its defaults and
    // leaves the dirty flag alone; overriding a real pick is an edit.
    const bool hadPick = settings.rendererObjectIndex != VSR_INVALID_INDEX;
    if (settings.rendererObjectIndex != renderer->index()
        || settings.rendererSubtype != renderer->subtype().str()
        || settings.rendererLibrary != m_libraryName) {
      settings.rendererObjectIndex = renderer->index();
      settings.rendererSubtype = renderer->subtype().str();
      settings.rendererLibrary = m_libraryName;
      if (hadPick)
        project.markDirty();
    }
  }
  m_renderer = renderer;

  // The shot camera is what the client orbits and what RenderShot renders
  // with; the scene's default camera is only a last resort.
  if (auto *camera = m_projectContext.resolveShotCamera(*shot)) {
    m_cameraIndex = camera->index();
  } else {
    vsr::core::logWarning(
        "[StudioServer] shot '%s' has no camera object; rendering with the"
        " scene's default camera",
        shot->id.c_str());
    m_cameraIndex = scene.defaultCamera().index();
  }

  if (m_scenePass) {
    m_scenePass->setRenderer(m_renderIndex->renderer(m_renderer->index()));
    m_scenePass->setCamera(m_renderIndex->camera(m_cameraIndex));
  }
  return true;
}

void StudioServer::unbindRendering()
{
  m_renderer = {};
  m_cameraIndex = VSR_INVALID_INDEX;
  if (m_scenePass) {
    m_scenePass->setRenderer(nullptr);
    m_scenePass->setCamera(nullptr);
  }
}

bool StudioServer::setupRendering(std::string *error)
{
  auto &scene = m_ctx.vsr.scene;
  auto *shot = project::activeShot(m_projectContext.project());

  m_renderIndex =
      m_ctx.anari.acquireRenderIndex(scene, m_libraryName, m_device);
  if (!m_renderIndex) {
    if (error)
      *error = "failed to create the render index";
    return false;
  }

  if (!bindActiveShotRendering(error))
    return false;
  m_boundShotRevision = m_projectContext.activeShotRevision();

  const auto &settings = shot->renderSettings;
  m_frameWidth = settings.width;
  m_frameHeight = settings.height;
  m_pipeline.setDimensions(m_frameWidth, m_frameHeight);

  m_scenePass =
      m_pipeline.emplace_back<vsr::rendering::AnariSceneRenderPass>(m_device);
  // Blocking renders keep each Frame consistent with the edits applied just
  // before it; the one-in-flight rule already paces the loop.
  m_scenePass->setRunAsync(false);
  m_scenePass->setColorFormat(ANARI_UFIXED8_RGBA_SRGB);
  m_scenePass->setWorld(m_renderIndex->world());
  m_scenePass->setRenderer(m_renderIndex->renderer(m_renderer->index()));
  m_scenePass->setCamera(m_renderIndex->camera(m_cameraIndex));
  m_scenePass->setEnableIDs(false);
  // The id-driven passes composite over the LDR color before it is copied
  // out; the server has no tonemap stage in between.
  m_viewport.setup(m_pipeline, m_scenePass, m_device);

  auto *copy =
      m_pipeline.emplace_back<vsr::rendering::CopyFromColorBufferPass>();
  copy->setExternalBuffer(m_colorBytes);

  m_push = scene.updateDelegate().emplace<ServerPushDelegate>(
      &scene,
      [this](Message &&msg) { send(std::move(msg)); },
      [this]() { m_session.sceneResendPending = true; });

  vsr::core::logStatus(
      "[StudioServer] rendering shot '%s' with renderer '%s' at %ux%u",
      shot->id.c_str(),
      m_renderer->subtype().c_str(),
      m_frameWidth,
      m_frameHeight);
  return true;
}

bool StudioServer::setupNetwork(std::string *error)
{
  try {
    m_server = std::make_shared<vsr::network::NetworkServer>(m_options.port);
  } catch (const std::exception &e) {
    if (error) {
      *error = "cannot listen on port " + std::to_string(m_options.port) + ": "
          + e.what();
    }
    return false;
  }

  m_server->setConnectHandler([this]() { onConnected(); });
  m_server->setDisconnectHandler(
      [this](const boost::system::error_code &ec) { onDisconnected(ec); });
  // Single client per server: the one being replaced hears why before its
  // socket closes (the transport lets the queue drain, briefly, first). The
  // farewell is the one server-initiated close of a session it did not
  // lose; a refused Hello is answered with an Error, and a Shutdown or a
  // client's own Disconnect closes at the client's request.
  m_server->setReplaceHandler([this]() {
    Disconnect farewell;
    farewell.reason = "replaced by another client";
    m_server->send(encode(farewell));
  });
  // Every one of the 256 type bytes gets a handler so a message outside the
  // Studio set is answered with an Error instead of vanishing in the
  // transport's "no handler" log.
  for (int value = 0; value <= 0xff; ++value) {
    m_server->registerHandler(
        uint8_t(value), [this](const Message &msg) { onMessage(msg); });
  }
  m_server->start();
  return true;
}

void StudioServer::teardown()
{
  if (m_server) {
    m_server->stop();
    m_server->removeAllHandlers();
    m_server.reset();
  }

  auto &scene = m_ctx.vsr.scene;
  if (m_push) {
    scene.updateDelegate().erase(m_push);
    m_push = nullptr;
  }
  // Passes hold ANARI handles: release them before the device goes.
  m_viewport.teardown();
  m_session.pendingPick.reset();
  m_pipeline.clear();
  m_scenePass = nullptr;
  m_renderer = {};
  m_renderers.clear();
  if (m_renderIndex) {
    m_ctx.anari.releaseRenderIndex(scene, m_device);
    m_renderIndex = nullptr;
  }
  if (m_device) {
    anari::release(m_device, m_device);
    m_device = nullptr;
  }
  m_ctx.anari.releaseAllDevices();
  m_started = false;
}

// Render loop ////////////////////////////////////////////////////////////////

void StudioServer::run()
{
  if (!m_started) {
    vsr::core::logError(
        "[StudioServer] run() called before a successful start()");
    return;
  }

  vsr::core::logStatus("[StudioServer] Listening on port %u", port());

  while (!m_shutdownRequested.load()) {
    applyControlState();
    if (m_state == SessionState::Bootstrapping)
      bootstrap();
    if (m_session.sceneResendPending)
      sendSceneSnapshot();
    // One Server Task per iteration; frames wait while it runs. A shot
    // render outlives its session (it runs with nobody listening and the
    // next bootstrap replays its ending).
    if (sessionEstablished() || m_dispatcher.renderActive())
      runOneTask();
    followProjectRevisions();
    // A Frame still on the wire gets a moment to finish before time moves
    // on, so a fast link never sees a header skip a frame; on a slow link
    // the tick goes ahead regardless and time keeps its pace.
    if (m_streaming && !vsr::network::is_ready(m_session.frameInFlight))
      m_session.frameInFlight.wait_for(PAUSED_SLEEP);
    // Time advances before the render so the Frame carries the frame drawn.
    m_playback.tick(sessionEstablished());
    m_playback.commitScrubIfQuiet();
    followProjectRevisions();

    // A pick renders its own frame, with ids, paused or not.
    bool frameSent = false;
    if (m_session.pendingPick && sessionEstablished())
      frameSent = servicePendingPick();

    if (m_streaming) {
      if (!frameSent)
        renderAndSendFrame();
    } else if (m_state == SessionState::Listening) {
      std::this_thread::sleep_for(LISTENING_SLEEP);
    } else {
      std::this_thread::sleep_for(PAUSED_SLEEP);
    }
  }

  vsr::core::logStatus("[StudioServer] Shutting down");
  setState(SessionState::Shutdown);
  teardown();
}

void StudioServer::requestShutdown()
{
  m_shutdownRequested.store(true);
  m_tasks.stopAll();
}

void StudioServer::runOneTask()
{
  const auto ran = m_tasks.runOne();
  if (!ran)
    return;
  if (ran->exclusive) {
    // Whatever the render body latched while it held the loop targeted the
    // scene it was mutating; drop it before the ending, so nothing sent in
    // reaction to the ending is lost with it.
    std::lock_guard lock(m_controlMutex);
    discardStaleInputs(m_control);
  }
  m_tasks.sendEnding(*ran);
}

void StudioServer::applyControlState()
{
  ControlState control;
  {
    std::lock_guard lock(m_controlMutex);
    control = std::move(m_control);
    m_control = ControlState{};
  }

  // Session events first, in the order they happened (a loss, an accept and
  // the new client's Hello may share one batch). A close the server asked
  // for acts on the socket only while it still has one to act on.
  for (auto &event : control.events) {
    const bool socketGone = event.kind == SessionEvent::Kind::CloseRequested
        && socketClosedInBatch(event, control.events);
    applySessionEvent(event, socketGone);
  }

  const bool bootstrapping = m_state == SessionState::Bootstrapping;
  if (!bootstrapping && !sessionEstablished()) {
    if (control.hasInput()) {
      vsr::core::logWarning(
          "[StudioServer] control messages dropped: no session (%s)",
          toString(m_state));
    }
    return;
  }

  if (control.encodings) {
    const auto encoding = negotiateFrameEncoding(control.encodings->supported);
    if (encoding != m_session.encoding) {
      m_session.encoding = encoding;
      vsr::core::logStatus(
          "[StudioServer] frame encoding: %s", toString(m_session.encoding));
    }
  }

  if (control.frameConfig) {
    applyFrameConfig(control.frameConfig->width, control.frameConfig->height);
    // The bootstrap carries its own FrameConfig; outside it the client learns
    // the effective size from this ack.
    if (!bootstrapping) {
      FrameConfig config;
      config.width = m_frameWidth;
      config.height = m_frameHeight;
      send(encode(config));
    }
  }

  if (!control.edits.empty()) {
    // Origin-based echo suppression: what the client just told us must not be
    // pushed back at it.
    const bool pushWasEnabled = m_push && m_push->enabled();
    setPushEnabled(false);
    for (const auto &edit : control.edits)
      std::visit([this](const auto &e) { applyEdit(e); }, edit);
    setPushEnabled(pushWasEnabled);
  }

  if (control.time)
    m_playback.applyTime(*control.time, !bootstrapping);
  applyViewportControl(control);

  for (auto &request : control.requests)
    m_session.pendingRequests.push_back(std::move(request));
  // Replies must not run ahead of the bootstrap bracket.
  if (!bootstrapping)
    dispatchPendingRequests();

  if (control.rendering)
    setStreaming(*control.rendering);
}

bool StudioServer::ControlState::hasInput() const
{
  return frameConfig || encodings || rendering || !edits.empty()
      || !requests.empty() || time || pick || outline || viewportSettings;
}

bool StudioServer::socketClosedInBatch(
    const SessionEvent &close, const std::vector<SessionEvent> &events)
{
  // The transport holds one socket: a connection accepted since replaced it
  // (serials climb), and a loss of the same connection means the peer closed
  // it. Restarting the transport for either would only cut off whoever holds
  // the socket now.
  return std::any_of(events.begin(), events.end(), [&](const auto &other) {
    return (other.kind == SessionEvent::Kind::Accepted
               && other.serial > close.serial)
        || (other.kind == SessionEvent::Kind::Lost
            && other.serial == close.serial);
  });
}

void StudioServer::applySessionEvent(SessionEvent &event, bool socketGone)
{
  const bool onSession =
      m_state != SessionState::Listening && event.serial == m_session.serial;
  switch (event.kind) {
  case SessionEvent::Kind::Accepted:
    beginSession(event.serial);
    break;
  case SessionEvent::Kind::Hello:
    if (!onSession) {
      vsr::core::logWarning(
          "[StudioServer] Hello from a connection that is gone; ignored");
    } else if (m_state == SessionState::AwaitingHello) {
      setState(SessionState::Bootstrapping);
    } else {
      vsr::core::logWarning(
          "[StudioServer] duplicate Hello ignored (%s)", toString(m_state));
    }
    break;
  case SessionEvent::Kind::Lost:
    if (onSession)
      endSession(event.reason, false);
    break;
  case SessionEvent::Kind::CloseRequested:
    // Nothing left to close: the loss or accept that follows ends the session.
    if (!onSession || socketGone)
      break;
    if (event.farewell.valid())
      event.farewell.wait_for(FAREWELL_TIMEOUT);
    endSession(event.reason, true);
    break;
  }
}

void StudioServer::discardStaleInputs(ControlState &control)
{
  if (!control.edits.empty() || control.time) {
    vsr::core::logWarning(
        "[StudioServer] %zu scene edit(s)%s latched during the shot render"
        " dropped: they targeted the scene the render was mutating",
        control.edits.size(),
        control.time ? " and a SetTime" : "");
    control.edits.clear();
    control.time.reset();
  }
  if (control.pick) {
    // Pick has no reply-with-error of its own; the bare Error names it.
    replyError("Pick " + std::to_string(control.pick->requestId)
        + " refused: render in progress");
    control.pick.reset();
  }
}

void StudioServer::dispatchPendingRequests()
{
  while (!m_session.pendingRequests.empty()) {
    // A sync op the client sent after a task waits for that task to run, so
    // the project sees requests in the order they were sent; task ops only
    // queue (their TaskStarted goes out now). While the front waits, a
    // cancel or browse behind it is served out of turn: it touches nothing
    // the task or the sync op could, and a cancel that waited for the task
    // it names would always come too late.
    auto next = m_session.pendingRequests.begin();
    // A request the dispatcher will refuse anyway need not wait its turn.
    const bool refusedNow = m_dispatcher.refuses(*next);
    if (!refusedNow && m_tasks.queued() > 0 && waitsForQueuedTasks(*next)) {
      next = std::find_if(std::next(next),
          m_session.pendingRequests.end(),
          independentOfQueuedTasks);
      if (next == m_session.pendingRequests.end())
        break;
    }
    auto request = std::move(*next);
    m_session.pendingRequests.erase(next);
    m_dispatcher.dispatch(request);
    // Each mutation gets its own snapshot, right after its reply; a task
    // queued here sees its prelude's snapshot before its first progress.
    followProjectRevisions();
  }
}

void StudioServer::beginSession(uint64_t serial)
{
  if (m_state != SessionState::Listening) {
    // The transport accepted a second socket over the first; the old client
    // is gone from our point of view either way.
    vsr::core::logWarning(
        "[StudioServer] new connection replaces the current session (%s)",
        toString(m_state));
  }
  resetSession();
  m_session.serial = serial;
  // Viewport state belongs to the client that sent it.
  m_viewport.apply(ViewportSettings{});
  m_viewport.setOutline({}, m_ctx.vsr.scene);
  setState(SessionState::AwaitingHello);
  vsr::core::logStatus("[StudioServer] client connected, awaiting Hello");
}

void StudioServer::endSession(const std::string &reason, bool closeSocket)
{
  vsr::core::logStatus("[StudioServer] session ended: %s", reason.c_str());
  if (!m_session.pendingRequests.empty()) {
    vsr::core::logWarning(
        "[StudioServer] %zu pending project request(s) dropped with the"
        " session",
        m_session.pendingRequests.size());
  }
  resetSession();
  if (closeSocket) {
    // Closes the socket and re-arms the accept; the disconnect this reports
    // lands in the latch and is dropped as stale. A peer-closed socket needs
    // neither: the transport re-arms its accept after every connection.
    m_server->restart();
  }
  setState(SessionState::Listening);
  vsr::core::logStatus("[StudioServer] Listening on port %u", port());
}

void StudioServer::resetSession()
{
  m_session = {};
  setPushEnabled(false);
  setStreaming(false);
  m_tasks.dropQueued();
  m_playback.cancelScrub();
}

void StudioServer::bootstrap()
{
  auto &scene = m_ctx.vsr.scene;
  vsr::core::logStatus("[StudioServer] bootstrapping client");

  send(encode(BootstrapBegin{}));
  {
    messages::TransferScene transfer(&scene, false);
    send(encodeSceneMessage<StudioMessageType::TransferScene>(transfer));
  }
  for (size_t i = 0; i < scene.numberOfLayers(); ++i) {
    messages::TransferLayer transfer(&scene, scene.layer(i));
    send(encodeSceneMessage<StudioMessageType::TransferLayer>(transfer));
  }
  FrameConfig config;
  config.width = m_frameWidth;
  config.height = m_frameHeight;
  send(encode(config));
  send(encode(UIState{m_uiState}));
  // Task-status replay: how the tasks this client never heard about ended.
  m_tasks.replayTo([this](Message &&msg) { send(std::move(msg)); });
  sendProjectSnapshot();
  send(encode(BootstrapEnd{}));

  setState(SessionState::Established);
  setPushEnabled(true);
}

void StudioServer::followProjectRevisions()
{
  // The bind first: it records the renderer pick in the shot, which the
  // snapshot then carries.
  if (m_projectContext.activeShotRevision() != m_boundShotRevision) {
    m_boundShotRevision = m_projectContext.activeShotRevision();
    std::string error;
    if (!bindActiveShotRendering(&error)) {
      // Unbound now: frames pause until an op that gives the project an
      // active shot (CreateShot, another open) binds again.
      vsr::core::logError(
          "[StudioServer] cannot render the active shot: %s; frames paused",
          error.c_str());
    }
  }
  // With nobody listening the revision drifts; the next bootstrap's snapshot
  // catches the client up and records where it stands.
  if (sessionEstablished() && m_projectContext.revision() != m_snapshotRevision)
    sendProjectSnapshot();
}

void StudioServer::sendProjectSnapshot()
{
  m_snapshotRevision = m_projectContext.revision();
  send(encode(ProjectSnapshot{m_projectContext.project()}));
}

void StudioServer::sendSceneSnapshot()
{
  m_session.sceneResendPending = false;
  if (!sessionEstablished())
    return;
  messages::TransferScene transfer(&m_ctx.vsr.scene, false);
  send(encodeSceneMessage<StudioMessageType::TransferScene>(transfer));
}

void StudioServer::applyFrameConfig(uint32_t width, uint32_t height)
{
  if (width == m_frameWidth && height == m_frameHeight)
    return;
  m_frameWidth = width;
  m_frameHeight = height;
  // AnariSceneRenderPass re-derives the camera aspect from its dimensions.
  m_pipeline.setDimensions(width, height);
  vsr::core::logStatus("[StudioServer] frame size: %ux%u", width, height);
}

void StudioServer::applyEdit(const SetObjectParameter &edit)
{
  auto *obj =
      m_ctx.vsr.scene.getObject(edit.object.type, edit.object.objectIndex);
  if (!obj) {
    vsr::core::logWarning(
        "[StudioServer] SetObjectParameter '%s' on unknown object (%s, %zu)",
        edit.name.c_str(),
        anari::toString(edit.object.type),
        edit.object.objectIndex);
    return;
  }
  obj->addParameter(edit.name).setValue(edit.value);
  followCameraEdit(obj);
}

void StudioServer::applyEdit(const RemoveObjectParameter &edit)
{
  auto *obj =
      m_ctx.vsr.scene.getObject(edit.object.type, edit.object.objectIndex);
  if (!obj) {
    vsr::core::logWarning(
        "[StudioServer] RemoveObjectParameter '%s' on unknown object (%s, %zu)",
        edit.name.c_str(),
        anari::toString(edit.object.type),
        edit.object.objectIndex);
    return;
  }
  obj->removeParameter(edit.name);
}

void StudioServer::applyEdit(const SetNodeTransform &edit)
{
  auto &scene = m_ctx.vsr.scene;
  auto *layer = scene.layer(edit.node.layerName.c_str());
  auto node =
      layer ? layer->at(edit.node.nodeIndex) : vsr::scene::LayerNodeRef{};
  if (!node || !(*node)->isTransform()) {
    vsr::core::logWarning(
        "[StudioServer] SetNodeTransform on '%s'[%zu]: not a transform node",
        edit.node.layerName.c_str(),
        edit.node.nodeIndex);
    return;
  }
  (*node)->setAsTransform(edit.transform);
  scene.signalLayerTransformChanged(layer);
}

void StudioServer::renderAndSendFrame()
{
  // Nothing to render with (a project opened without an active shot); the
  // rebind that follows the next shot-changing op resumes frames.
  if (!m_renderer) {
    std::this_thread::sleep_for(PAUSED_SLEEP);
    return;
  }
  // Latest-frame-wins: the previous Frame is still on the wire (the loop
  // already waited a moment for it), so this iteration's picture would be
  // dropped anyway; do not render it.
  if (!vsr::network::is_ready(m_session.frameInFlight))
    return;

  prepareViewportPasses();
  m_pipeline.render();
  sendRenderedFrame();
}

void StudioServer::sendRenderedFrame()
{
  const size_t expectedBytes = size_t(m_frameWidth) * m_frameHeight * 4;
  if (m_colorBytes.size() != expectedBytes) {
    vsr::core::logError(
        "[StudioServer] color buffer holds %zu bytes, expected %zu; frame"
        " dropped",
        m_colorBytes.size(),
        expectedBytes);
    return;
  }

  if (!encodeFramePixels(m_session.encoding,
          m_frameWidth,
          m_frameHeight,
          m_colorBytes.data(),
          m_encodedPixels)) {
    vsr::core::logError(
        "[StudioServer] %s frame encoding failed; frame dropped",
        toString(m_session.encoding));
    return;
  }

  const auto &project = m_projectContext.project();
  const auto *shot = project::activeShot(project);
  FrameHeader header;
  header.width = m_frameWidth;
  header.height = m_frameHeight;
  header.pixelFormat = PixelFormat::RGBA8_sRGB;
  header.encoding = m_session.encoding;
  header.shotId = project.activeShotId;
  // Time in Motion: the playback tick ran before this render, so this is the
  // frame the pixels show.
  header.frame = shot ? shot->currentFrame : 0;
  m_session.frameInFlight = m_server->send(
      encodeFrame(header, m_encodedPixels.data(), m_encodedPixels.size()));
}

void StudioServer::send(Message &&msg)
{
  if (m_server)
    m_server->send(std::move(msg));
}

bool StudioServer::sessionEstablished() const
{
  return m_state.load() == SessionState::Established;
}

void StudioServer::followCameraEdit(const vsr::scene::Object *object)
{
  auto *shot = project::activeShot(m_projectContext.project());
  if (!shot || !object || object->type() != ANARI_CAMERA
      || m_projectContext.resolveShotCamera(*shot) != object)
    return;

  auto &manipulator = m_ctx.view.manipulator;
  vsr::rendering::updateManipulatorFromCameraPose(
      manipulator, *static_cast<const vsr::scene::Camera *>(object));
  // Without keyframes the rig's current view is what applyActiveShot() writes
  // back into the camera on every time change; it follows the client's view
  // here (no snapshot: the view is in motion, like time under playback).
  if (auto *rig = m_projectContext.activeShotCameraRig();
      rig && rig->keyframes.empty()) {
    rig->current = camera_rig::manipulatorStateFromManipulator(manipulator);
  }
}

void StudioServer::setState(SessionState state)
{
  const auto previous = m_state.exchange(state);
  if (previous != state) {
    vsr::core::logStatus(
        "[StudioServer] %s -> %s", toString(previous), toString(state));
  }
}

void StudioServer::setStreaming(bool streaming)
{
  if (m_streaming.exchange(streaming) != streaming) {
    vsr::core::logStatus(
        "[StudioServer] streaming %s", streaming ? "on" : "off");
  }
}

void StudioServer::setPushEnabled(bool enabled)
{
  if (m_push)
    m_push->setEnabled(enabled);
}

// Viewport ///////////////////////////////////////////////////////////////////

void StudioServer::applyViewportControl(const ControlState &control)
{
  if (control.viewportSettings)
    m_viewport.apply(*control.viewportSettings);
  if (control.outline)
    m_viewport.setOutline(control.outline->objectIdentity, m_ctx.vsr.scene);
  if (control.pick) {
    if (m_session.pendingPick) {
      vsr::core::logStatus(
          "[StudioServer] Pick %llu superseded before it was serviced",
          static_cast<unsigned long long>(m_session.pendingPick->requestId));
    }
    m_session.pendingPick = *control.pick;
  }
}

const vsr::scene::Object *StudioServer::shotCameraObject() const
{
  return m_ctx.vsr.scene.getObject(ANARI_CAMERA, m_cameraIndex);
}

void StudioServer::prepareViewportPasses()
{
  m_viewport.updateWorldBounds(
      m_renderIndex ? m_renderIndex->world() : nullptr, shotCameraObject());
}

bool StudioServer::servicePendingPick()
{
  const Pick pick = *m_session.pendingPick;
  m_session.pendingPick.reset();

  // Nothing to render with (a project without an active shot), like
  // renderAndSendFrame(): refused rather than answered as a miss, so the
  // client does not clear its selection over it.
  if (!m_renderer || m_frameWidth == 0 || m_frameHeight == 0) {
    Error error;
    error.message = "Pick " + std::to_string(pick.requestId)
        + " refused: no active shot to render";
    vsr::core::logWarning("[StudioServer] %s", error.message.c_str());
    send(encode(error));
    return false;
  }

  m_viewport.armPick(pick.x, pick.y);
  prepareViewportPasses();
  m_pipeline.render();
  const auto sample = m_viewport.takePick();

  PickReply reply;
  reply.requestId = pick.requestId;
  reply.objectIdentity = sample ? sample->identity() : std::nullopt;
  reply.hit = reply.objectIdentity.has_value();
  if (reply.hit) {
    if (const auto *camera = shotCameraObject()) {
      reply.worldPosition = pickWorldPosition(readCameraView(*camera),
          m_frameWidth,
          m_frameHeight,
          pick.x,
          pick.y,
          sample->depth);
    }
    vsr::core::logStatus(
        "[StudioServer] Pick %llu at (%d, %d): %s %zu, depth %f",
        static_cast<unsigned long long>(pick.requestId),
        pick.x,
        pick.y,
        anari::toString(reply.objectIdentity->type),
        reply.objectIdentity->objectIndex,
        sample->depth);
  } else {
    vsr::core::logStatus("[StudioServer] Pick %llu at (%d, %d): background",
        static_cast<unsigned long long>(pick.requestId),
        pick.x,
        pick.y);
  }
  send(encode(reply));

  // The frame that carried the ids is as good as any: send it when the
  // client is streaming and the previous one is off the wire.
  if (m_streaming && vsr::network::is_ready(m_session.frameInFlight)) {
    sendRenderedFrame();
    return true;
  }
  return false;
}

// IO thread //////////////////////////////////////////////////////////////////

void StudioServer::onConnected()
{
  ++m_connectionSerial;
  m_helloAccepted = false;
  Hello hello;
  hello.version = PROTOCOL_VERSION;
  hello.buildInfo = "scivisStudioServer/" + m_libraryName;
  m_server->send(encode(hello));
  latchSessionEvent({SessionEvent::Kind::Accepted, m_connectionSerial});
}

void StudioServer::onDisconnected(const boost::system::error_code &error)
{
  latchSessionEvent({SessionEvent::Kind::Lost,
      m_connectionSerial,
      error ? error.message() : std::string("connection closed")});
}

void StudioServer::onMessage(const Message &msg)
{
  const auto type = messageType(msg);
  if (!type) {
    replyError("unknown message type " + std::to_string(int(msg.header.type)));
    return;
  }

  switch (*type) {
  case StudioMessageType::Hello:
    onHello(msg);
    return;
  case StudioMessageType::Ping:
    m_server->send(encode(Pong{}));
    return;
  case StudioMessageType::Pong:
    return;
  case StudioMessageType::Error: {
    const auto error = decode<Error>(msg);
    vsr::core::logWarning("[StudioServer] client reports: %s",
        error ? error->message.c_str() : "(undecodable Error)");
    return;
  }
  case StudioMessageType::Disconnect:
    requestClose("client sent Disconnect");
    return;
  default:
    break;
  }

  if (!m_helloAccepted) {
    replyError(std::string(toString(*type)) + " received before Hello");
    return;
  }

  switch (*type) {
  case StudioMessageType::Shutdown:
    vsr::core::logStatus("[StudioServer] Shutdown requested by client");
    requestShutdown();
    return;
  case StudioMessageType::SetFrameConfig: {
    // The one input checked before it is latched: a hostile size is refused
    // here, not applied to the pipeline.
    const auto config = decodeOrRefuse<SetFrameConfig>(msg);
    if (!config)
      return;
    if (config->width == 0 || config->height == 0
        || config->width > MAX_FRAME_DIMENSION
        || config->height > MAX_FRAME_DIMENSION) {
      replyError("SetFrameConfig " + std::to_string(config->width) + "x"
          + std::to_string(config->height) + " is outside 1.."
          + std::to_string(MAX_FRAME_DIMENSION));
      return;
    }
    std::lock_guard lock(m_controlMutex);
    m_control.frameConfig = *config;
    return;
  }
  case StudioMessageType::SetEncodings:
    latch(msg, &ControlState::encodings);
    return;
  case StudioMessageType::StartRendering:
  case StudioMessageType::StopRendering: {
    std::lock_guard lock(m_controlMutex);
    m_control.rendering = *type == StudioMessageType::StartRendering;
    return;
  }
  case StudioMessageType::SetObjectParameter:
    latchEdit<SetObjectParameter>(msg);
    return;
  case StudioMessageType::RemoveObjectParameter:
    latchEdit<RemoveObjectParameter>(msg);
    return;
  case StudioMessageType::SetNodeTransform:
    latchEdit<SetNodeTransform>(msg);
    return;
  case StudioMessageType::SetTime:
    // The scrub: optimistic and latest-wins, so a latch slot.
    latch(msg, &ControlState::time);
    return;
  // Viewport: latest-wins slots
  case StudioMessageType::Pick:
    latch(msg, &ControlState::pick);
    return;
  case StudioMessageType::SetOutline:
    latch(msg, &ControlState::outline);
    return;
  case StudioMessageType::ViewportSettings:
    latch(msg, &ControlState::viewportSettings);
    return;
  default:
    break;
  }

  if (isProjectRequestType(*type)) {
    auto request = decodeProjectRequest(msg);
    if (!request) {
      refuseRequest(
          msg, "malformed " + std::string(toString(*type)) + " payload");
      return;
    }
    // A cancel of the running task cannot wait for the loop (it is inside
    // the body): the flag is raised here, the request still goes through
    // the queue for its reply.
    if (const auto *cancel = std::get_if<CancelTask>(&*request))
      m_tasks.requestCancelRunning(cancel->taskId);
    std::lock_guard lock(m_controlMutex);
    m_control.requests.push_back(std::move(*request));
    return;
  }

  if (isServerToClient(*type)) {
    replyError(std::string(toString(*type)) + " is a server-to-client message");
  } else {
    // Every client-to-server type is handled above; a new one added to the
    // protocol without a handler is refused loudly, never dropped.
    refuseRequest(
        msg, std::string(toString(*type)) + " is not served by this server");
  }
}

void StudioServer::onHello(const Message &msg)
{
  const auto hello = decode<Hello>(msg);
  if (!hello) {
    replyError("malformed Hello payload");
    return;
  }

  if (hello->version != PROTOCOL_VERSION) {
    Error error;
    error.message = "protocol version mismatch: client speaks v"
        + std::to_string(hello->version) + ", this server v"
        + std::to_string(PROTOCOL_VERSION);
    vsr::core::logWarning("[StudioServer] %s", error.message.c_str());
    requestClose(error.message, m_server->send(encode(error)));
    return;
  }

  m_helloAccepted = true;
  latchSessionEvent({SessionEvent::Kind::Hello, m_connectionSerial});
}

void StudioServer::latchSessionEvent(SessionEvent event)
{
  std::lock_guard lock(m_controlMutex);
  m_control.events.push_back(std::move(event));
}

void StudioServer::requestClose(
    const std::string &reason, vsr::network::MessageFuture farewell)
{
  latchSessionEvent({SessionEvent::Kind::CloseRequested,
      m_connectionSerial,
      reason,
      std::move(farewell)});
}

void StudioServer::replyError(const std::string &text)
{
  vsr::core::logWarning("[StudioServer] %s", text.c_str());
  Error error;
  error.message = text;
  m_server->send(encode(error));
}

void StudioServer::refuseRequest(const Message &msg, const std::string &text)
{
  // 0 is the never-minted id (RequestHandle::valid), so nobody waits on it.
  const auto requestId = peekRequestId(msg);
  if (!requestId || *requestId == 0) {
    replyError(text);
    return;
  }
  vsr::core::logWarning("[StudioServer] request %llu refused: %s",
      static_cast<unsigned long long>(*requestId),
      text.c_str());
  m_server->send(encode(makeErrorReply(*requestId, text)));
}

} // namespace vsr::scivis_studio::server
