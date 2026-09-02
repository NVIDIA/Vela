// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "ProjectSnapshot.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Renderer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <chrono>
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

// Types the server emits; a client sending one is confused, not ahead of us.
constexpr bool isServerToClient(StudioMessageType type)
{
  switch (type) {
  case StudioMessageType::BootstrapBegin:
  case StudioMessageType::BootstrapEnd:
  case StudioMessageType::ProjectOpReply:
  case StudioMessageType::ProjectSnapshot:
  case StudioMessageType::TaskProgress:
  case StudioMessageType::TaskCompleted:
  case StudioMessageType::TaskFailed:
  case StudioMessageType::TimeAdvanceWarning:
  case StudioMessageType::PickReply:
  case StudioMessageType::UIState:
  case StudioMessageType::TransferScene:
  case StudioMessageType::TransferLayer:
  case StudioMessageType::ObjectAdded:
  case StudioMessageType::ObjectRemoved:
  case StudioMessageType::FrameConfig:
  case StudioMessageType::Frame:
    return true;
  default:
    return false;
  }
}

} // namespace

const char *toString(SessionState state)
{
  switch (state) {
  case SessionState::Listening:
    return "Listening";
  case SessionState::AwaitingHello:
    return "AwaitingHello";
  case SessionState::Connected:
    return "Connected";
  case SessionState::Rendering:
    return "Rendering";
  case SessionState::Shutdown:
    return "Shutdown";
  }
  return "Unknown";
}

// Construction ///////////////////////////////////////////////////////////////

StudioServer::StudioServer(const ServerOptions &options)
    : m_options(options), m_projectContext(&m_ctx)
{}

StudioServer::~StudioServer()
{
  teardown();
}

// Queries ////////////////////////////////////////////////////////////////////

unsigned short StudioServer::port() const
{
  return m_server ? m_server->port() : 0;
}

SessionState StudioServer::sessionState() const
{
  return m_state.load();
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

// Startup ////////////////////////////////////////////////////////////////////

bool StudioServer::start(std::string *error)
{
  if (m_started) {
    if (error)
      *error = "server already started";
    return false;
  }

  for (const auto &root : m_options.dataRoots)
    vsr::core::logStatus("[StudioServer] Data Root: %s", root.c_str());

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
    std::string openError;
    if (!m_projectContext.openProject(m_options.projectDirectory,
            nullptr,
            nullptr,
            nullptr,
            &openError)) {
      if (error) {
        *error = "failed to open project '"
            + m_options.projectDirectory.string() + "': " + openError;
      }
      return false;
    }
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

bool StudioServer::setupRendering(std::string *error)
{
  auto &scene = m_ctx.vsr.scene;
  auto &project = m_projectContext.project();
  auto *shot = project::activeShot(project);

  m_renderIndex =
      m_ctx.anari.acquireRenderIndex(scene, m_libraryName, m_device);
  if (!m_renderIndex) {
    if (error)
      *error = "failed to create the render index";
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
    if (error)
      *error = "ANARI library '" + m_libraryName + "' offers no renderers";
    return false;
  }

  if (settings.rendererObjectIndex != VSR_INVALID_INDEX) {
    auto renderer =
        scene.getObject<vsr::scene::Renderer>(settings.rendererObjectIndex);
    if (renderer && renderer->rendererDeviceName() == m_libraryName)
      m_renderer = renderer;
  }
  if (!m_renderer) {
    m_renderer = m_renderers.front();
    if (settings.rendererObjectIndex != m_renderer->index()
        || settings.rendererSubtype != m_renderer->subtype().str()
        || settings.rendererLibrary != m_libraryName) {
      settings.rendererObjectIndex = m_renderer->index();
      settings.rendererSubtype = m_renderer->subtype().str();
      settings.rendererLibrary = m_libraryName;
      project.markDirty();
    }
  }

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

  auto *copy =
      m_pipeline.emplace_back<vsr::rendering::CopyFromColorBufferPass>();
  copy->setExternalBuffer(m_colorBytes);

  m_push = scene.updateDelegate().emplace<ServerPushDelegate>(
      &scene,
      [this](Message &&msg) { send(std::move(msg)); },
      [this]() { m_sceneResendPending = true; });

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
    m_server =
        std::make_shared<vsr::network::NetworkServer>(short(m_options.port));
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
  // Every type byte gets a handler so a message outside the Studio set is
  // answered with an Error instead of vanishing in the transport.
  for (int value = 1; value < vsr::network::MESSAGE_TYPE_INVALID; ++value) {
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
    if (m_bootstrapPending)
      bootstrap();
    if (m_sceneResendPending)
      sendSceneSnapshot();

    switch (m_state.load()) {
    case SessionState::Rendering:
      renderAndSendFrame();
      break;
    case SessionState::Listening:
      std::this_thread::sleep_for(LISTENING_SLEEP);
      break;
    default:
      std::this_thread::sleep_for(PAUSED_SLEEP);
      break;
    }
  }

  vsr::core::logStatus("[StudioServer] Shutting down");
  setState(SessionState::Shutdown);
  teardown();
}

void StudioServer::requestShutdown()
{
  m_shutdownRequested.store(true);
}

void StudioServer::applyControlState()
{
  ControlState control;
  {
    std::lock_guard lock(m_controlMutex);
    control = std::move(m_control);
    m_control = ControlState{};
  }

  // Session events first, in causal order. Each names its connection: a loss
  // of the session in progress ends it, an accept opens the next one, and a
  // loss, Hello or close request for that newest connection is applied to it
  // (all of these may share one batch). Events for a connection that is
  // neither are stale and dropped.
  const auto lossOfCurrent = [&] {
    return control.disconnected && m_state != SessionState::Listening
        && *control.disconnected == m_sessionSerial;
  };
  if (lossOfCurrent())
    endSession(control.disconnectReason, false);
  if (control.accepted)
    beginSession(*control.accepted);
  if (lossOfCurrent())
    endSession(control.disconnectReason, false);
  if (control.closeRequested && m_state != SessionState::Listening
      && *control.closeRequested == m_sessionSerial) {
    if (control.farewell.valid())
      control.farewell.wait_for(FAREWELL_TIMEOUT);
    endSession(control.closeReason, true);
  }
  if (control.helloReceived) {
    if (*control.helloReceived != m_sessionSerial) {
      vsr::core::logWarning(
          "[StudioServer] Hello from a connection that is gone; ignored");
    } else if (m_state == SessionState::AwaitingHello) {
      m_bootstrapPending = true;
    } else {
      vsr::core::logWarning(
          "[StudioServer] duplicate Hello ignored (%s)", toString(m_state));
    }
  }

  const bool established = m_bootstrapPending
      || m_state == SessionState::Connected
      || m_state == SessionState::Rendering;
  if (!established) {
    const bool dropped = control.frameConfig || control.encoding
        || control.rendering || !control.edits.empty();
    if (dropped) {
      vsr::core::logWarning(
          "[StudioServer] control messages dropped: no session (%s)",
          toString(m_state));
    }
    return;
  }

  if (control.encoding && *control.encoding != m_encoding) {
    m_encoding = *control.encoding;
    vsr::core::logStatus(
        "[StudioServer] frame encoding: %s", toString(m_encoding));
  }

  if (control.frameConfig) {
    applyFrameConfig(control.frameConfig->width, control.frameConfig->height);
    // The bootstrap carries its own FrameConfig; outside it the client learns
    // the effective size from this ack.
    if (!m_bootstrapPending) {
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

  if (control.rendering)
    m_renderingRequested = *control.rendering;
  if (!m_bootstrapPending) {
    setState(m_renderingRequested ? SessionState::Rendering
                                  : SessionState::Connected);
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
  m_sessionSerial = serial;
  setPushEnabled(false);
  m_encoding = FrameEncoding::Raw;
  m_renderingRequested = false;
  m_bootstrapPending = false;
  m_sceneResendPending = false;
  m_frameInFlight = {};
  setState(SessionState::AwaitingHello);
  vsr::core::logStatus("[StudioServer] client connected, awaiting Hello");
}

void StudioServer::endSession(const std::string &reason, bool closeSocket)
{
  vsr::core::logStatus("[StudioServer] session ended: %s", reason.c_str());
  setPushEnabled(false);
  m_encoding = FrameEncoding::Raw;
  m_renderingRequested = false;
  m_bootstrapPending = false;
  m_sceneResendPending = false;
  m_frameInFlight = {};
  if (closeSocket) {
    // Closes the socket and re-arms the accept; the disconnect this reports
    // lands in the latch and is dropped as stale. A peer-closed socket needs
    // neither: the transport re-arms its accept after every connection.
    m_server->restart();
  }
  setState(SessionState::Listening);
  vsr::core::logStatus("[StudioServer] Listening on port %u", port());
}

void StudioServer::bootstrap()
{
  m_bootstrapPending = false;
  auto &scene = m_ctx.vsr.scene;
  vsr::core::logStatus("[StudioServer] bootstrapping client");

  send(encode(BootstrapBegin{}));
  {
    messages::TransferScene transfer(&scene, false);
    send(encodeSceneMessage(transfer, StudioMessageType::TransferScene));
  }
  for (size_t i = 0; i < scene.numberOfLayers(); ++i) {
    messages::TransferLayer transfer(&scene, scene.layer(i));
    send(encodeSceneMessage(transfer, StudioMessageType::TransferLayer));
  }
  FrameConfig config;
  config.width = m_frameWidth;
  config.height = m_frameHeight;
  send(encode(config));
  send(encode(ProjectSnapshot{m_projectContext.project()}));
  send(encode(BootstrapEnd{}));

  setState(
      m_renderingRequested ? SessionState::Rendering : SessionState::Connected);
  setPushEnabled(true);
}

void StudioServer::sendSceneSnapshot()
{
  m_sceneResendPending = false;
  if (m_state != SessionState::Connected && m_state != SessionState::Rendering)
    return;
  messages::TransferScene transfer(&m_ctx.vsr.scene, false);
  send(encodeSceneMessage(transfer, StudioMessageType::TransferScene));
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
  // Latest-frame-wins: the previous Frame is still on the wire, so this
  // iteration's picture would be dropped anyway; do not render it.
  if (!vsr::network::is_ready(m_frameInFlight)) {
    std::this_thread::sleep_for(PAUSED_SLEEP);
    return;
  }

  m_pipeline.render();

  const size_t expectedBytes = size_t(m_frameWidth) * m_frameHeight * 4;
  if (m_colorBytes.size() != expectedBytes) {
    vsr::core::logError(
        "[StudioServer] color buffer holds %zu bytes, expected %zu; frame"
        " dropped",
        m_colorBytes.size(),
        expectedBytes);
    return;
  }

  if (!encodeFramePixels(m_encoding,
          m_frameWidth,
          m_frameHeight,
          m_colorBytes.data(),
          m_encodedPixels)) {
    vsr::core::logError(
        "[StudioServer] %s frame encoding failed; frame dropped",
        toString(m_encoding));
    return;
  }

  const auto &project = m_projectContext.project();
  const auto *shot = project::activeShot(project);
  FrameHeader header;
  header.width = m_frameWidth;
  header.height = m_frameHeight;
  header.pixelFormat = PixelFormat::RGBA8_sRGB;
  header.encoding = m_encoding;
  header.shotId = project.activeShotId;
  header.frame = shot ? shot->currentFrame : 0; // Time at Rest: no playback yet
  m_frameInFlight = m_server->send(
      encodeFrame(header, m_encodedPixels.data(), m_encodedPixels.size()));
}

void StudioServer::send(Message &&msg)
{
  if (m_server)
    m_server->send(std::move(msg));
}

void StudioServer::setState(SessionState state)
{
  const auto previous = m_state.exchange(state);
  if (previous != state) {
    vsr::core::logStatus(
        "[StudioServer] %s -> %s", toString(previous), toString(state));
  }
}

void StudioServer::setPushEnabled(bool enabled)
{
  if (m_push)
    m_push->setEnabled(enabled);
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
  std::lock_guard lock(m_controlMutex);
  m_control.accepted = m_connectionSerial;
}

void StudioServer::onDisconnected(const boost::system::error_code &error)
{
  std::lock_guard lock(m_controlMutex);
  m_control.disconnected = m_connectionSerial;
  m_control.disconnectReason =
      error ? error.message() : std::string("connection closed");
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
    const auto config = decode<SetFrameConfig>(msg);
    if (!config) {
      replyError("malformed SetFrameConfig payload");
      return;
    }
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
  case StudioMessageType::SetEncodings: {
    const auto encodings = decode<SetEncodings>(msg);
    if (!encodings) {
      replyError("malformed SetEncodings payload");
      return;
    }
    std::lock_guard lock(m_controlMutex);
    m_control.encoding = negotiateFrameEncoding(encodings->supported);
    return;
  }
  case StudioMessageType::StartRendering:
  case StudioMessageType::StopRendering: {
    std::lock_guard lock(m_controlMutex);
    m_control.rendering = *type == StudioMessageType::StartRendering;
    return;
  }
  case StudioMessageType::SetObjectParameter: {
    auto edit = decode<SetObjectParameter>(msg);
    if (!edit) {
      replyError("malformed SetObjectParameter payload");
      return;
    }
    std::lock_guard lock(m_controlMutex);
    m_control.edits.emplace_back(std::move(*edit));
    return;
  }
  case StudioMessageType::RemoveObjectParameter: {
    auto edit = decode<RemoveObjectParameter>(msg);
    if (!edit) {
      replyError("malformed RemoveObjectParameter payload");
      return;
    }
    std::lock_guard lock(m_controlMutex);
    m_control.edits.emplace_back(std::move(*edit));
    return;
  }
  case StudioMessageType::SetNodeTransform: {
    auto edit = decode<SetNodeTransform>(msg);
    if (!edit) {
      replyError("malformed SetNodeTransform payload");
      return;
    }
    std::lock_guard lock(m_controlMutex);
    m_control.edits.emplace_back(std::move(*edit));
    return;
  }
  default:
    break;
  }

  if (isServerToClient(*type)) {
    replyError(std::string(toString(*type)) + " is a server-to-client message");
  } else {
    // Project Ops, Server Tasks, Remote Browse, playback, picking, RenderShot:
    // milestones 4-6. Refused loudly, never dropped.
    replyError(
        std::string(toString(*type)) + " is not implemented in this server");
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
  std::lock_guard lock(m_controlMutex);
  m_control.helloReceived = m_connectionSerial;
}

void StudioServer::requestClose(
    const std::string &reason, vsr::network::MessageFuture farewell)
{
  std::lock_guard lock(m_controlMutex);
  m_control.closeRequested = m_connectionSerial;
  m_control.closeReason = reason;
  m_control.farewell = std::move(farewell);
}

void StudioServer::replyError(const std::string &text)
{
  vsr::core::logWarning("[StudioServer] %s", text.c_str());
  Error error;
  error.message = text;
  m_server->send(encode(error));
}

} // namespace vsr::scivis_studio::server
