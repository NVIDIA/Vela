// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerConnection.h"
// vsr_scivis_studio_client_core
#include "MirrorUpdateDelegate.h"
#include "ProjectOps.h"
// vsr_scivis_studio_protocol
#include "ProjectOpReply.h"
#include "ProjectSnapshot.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
#include "ViewportMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_network
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>

namespace vsr::scivis_studio::client {

using namespace protocol;
namespace messages = vsr::network::messages;

namespace {

// Bound on the wait for a courtesy Disconnect/Shutdown to leave the socket;
// the UI thread never waits longer than this on the network.
constexpr std::chrono::milliseconds COURTESY_SEND_TIMEOUT{200};

std::string endpointText(const std::string &host, uint16_t port)
{
  return host + ":" + std::to_string(port);
}

} // namespace

const char *toString(ConnectionState state)
{
  switch (state) {
  case ConnectionState::NeverConnected:
    return "NeverConnected";
  case ConnectionState::Connected:
    return "Connected";
  case ConnectionState::Lost:
    return "Lost";
  case ConnectionState::Disconnected:
    return "Disconnected";
  }
  return "Unknown";
}

// Construction ///////////////////////////////////////////////////////////////

ServerConnection::ServerConnection(
    vsr::scene::Scene *mirror, ConnectionTimings timings)
    : m_mirror(mirror),
      m_timings(timings),
      m_channel(std::make_shared<vsr::network::NetworkClient>()),
      m_status("not connected")
{
  m_projectOps = std::make_unique<ProjectOps>(
      [this](vsr::network::Message &&msg) { return trySend(std::move(msg)); });
  if (m_mirror) {
    m_delegate = m_mirror->updateDelegate().emplace<MirrorUpdateDelegate>(
        [this](vsr::network::Message &&msg) { sendMessage(std::move(msg)); });
  }

  // Handlers are registered before any connect() so the IO thread never
  // observes the handler map changing. Every one of the 256 type bytes gets
  // one: a message outside the Studio set must be answered with an Error,
  // not dropped by the transport.
  for (int value = 0; value <= 0xff; ++value) {
    m_channel->registerHandler(uint8_t(value),
        [this](const vsr::network::Message &msg) { onInbound(msg); });
  }
  m_channel->setDisconnectHandler(
      [this](
          const boost::system::error_code &error) { onChannelClosed(error); });
}

ServerConnection::~ServerConnection()
{
  // Joining the IO thread first guarantees no handler runs against a
  // half-destroyed object.
  m_channel->disconnect();
  m_channel.reset();
  if (m_mirror && m_delegate)
    m_mirror->updateDelegate().erase(m_delegate);
}

// Queries ////////////////////////////////////////////////////////////////////

ConnectionState ServerConnection::state() const
{
  return m_state;
}

bool ServerConnection::autoRetrying() const
{
  return m_state == ConnectionState::Lost && m_autoRetryEnabled;
}

const std::string &ServerConnection::statusText() const
{
  return m_status;
}

const std::string &ServerConnection::host() const
{
  return m_host;
}

uint16_t ServerConnection::port() const
{
  return m_port;
}

bool ServerConnection::bootstrapping() const
{
  return m_bootstrapping;
}

bool ServerConnection::bootstrapped() const
{
  return m_bootstrapped;
}

const FrameConfig &ServerConnection::frameConfig() const
{
  return m_frameConfig;
}

const Project *ServerConnection::project() const
{
  return m_project.get();
}

bool ServerConnection::takeLatestFrame(vsr::network::Message &out)
{
  std::lock_guard lock(m_inboundMutex);
  if (!m_latestFrame)
    return false;
  out = std::move(*m_latestFrame);
  m_latestFrame.reset();
  if (const auto view = decodeFrame(out))
    m_lastFrameHeader = view->header;
  return true;
}

const std::optional<FrameHeader> &ServerConnection::lastFrameHeader() const
{
  return m_lastFrameHeader;
}

ProjectOps &ServerConnection::projectOps()
{
  return *m_projectOps;
}

const ProjectOps &ServerConnection::projectOps() const
{
  return *m_projectOps;
}

const std::optional<TimeAdvanceWarning> &
ServerConnection::lastTimeAdvanceWarning() const
{
  return m_timeAdvanceWarning;
}

void ServerConnection::clearTimeAdvanceWarning()
{
  m_timeAdvanceWarning.reset();
}

const SubtreePtr &ServerConnection::uiState() const
{
  return m_uiState;
}

// User intentions ////////////////////////////////////////////////////////////

void ServerConnection::connect(const std::string &host, uint16_t port)
{
  if (m_state == ConnectionState::Connected) {
    vsr::core::logWarning(
        "[ServerConnection] connect() ignored: already connected to %s",
        endpointText(m_host, m_port).c_str());
    return;
  }
  if (m_phase != Phase::Idle) {
    // A retry is in flight; redirecting it means starting over.
    m_phase = Phase::Idle;
    closeChannel();
  }
  m_host = host;
  m_port = port;
  if (m_state == ConnectionState::Lost) {
    // A user-driven connect re-arms the auto-retry window.
    m_autoRetryEnabled = true;
    m_lostAt = Clock::now();
    m_retryDelay = m_timings.retryInitialDelay;
  }
  beginAttempt();
}

void ServerConnection::disconnect()
{
  if (m_phase == Phase::Established) {
    auto sent = m_channel->send(encode(Disconnect{}));
    sent.wait_for(COURTESY_SEND_TIMEOUT);
  }
  dropSession("disconnected");
}

void ServerConnection::dropSession(const std::string &status)
{
  m_phase = Phase::Idle;
  closeChannel();
  m_autoRetryEnabled = false;
  m_bootstrapping = false;
  m_projectOps->failAllPending("connection lost");
  m_projectOps->clearTasks();
  clearMirror();
  m_project.reset();
  m_frameConfig = {};
  m_timeAdvanceWarning.reset();
  m_lastFrameHeader.reset();
  m_uiState.reset();
  m_farewellReason.clear();
  {
    std::lock_guard lock(m_inboundMutex);
    m_latestFrame.reset();
  }
  m_status = status;
  if (m_state != ConnectionState::NeverConnected)
    setState(ConnectionState::Disconnected);
}

void ServerConnection::retryNow()
{
  if (m_state != ConnectionState::Lost || m_phase != Phase::Idle)
    return;
  beginAttempt();
}

void ServerConnection::shutdownServer()
{
  if (m_phase == Phase::Established) {
    auto sent = m_channel->send(encode(Shutdown{}));
    sent.wait_for(COURTESY_SEND_TIMEOUT);
  }
  disconnect();
}

// Per-frame work /////////////////////////////////////////////////////////////

void ServerConnection::poll()
{
  const auto now = Clock::now();

  // 1. Inbound messages, on the UI thread only. What the server sent before
  // closing is handled before the close is: its farewell (a Disconnect with
  // the reason) must be read to become the loss reason.
  std::vector<vsr::network::Message> batch;
  {
    std::lock_guard lock(m_inboundMutex);
    batch.swap(m_inbound);
  }
  for (const auto &msg : batch) {
    if (m_phase == Phase::Idle)
      break; // handled message tore the connection down; drop the rest
    handleMessage(msg);
  }
  m_projectOps->poll();

  // 2. The IO thread's disconnect latch.
  if (m_ioDisconnected.exchange(false)) {
    boost::system::error_code error;
    {
      std::lock_guard lock(m_inboundMutex);
      error = m_ioDisconnectError;
    }
    const std::string reason = error ? error.message() : "connection closed";
    if (m_phase == Phase::Established)
      declareLoss(reason);
    else if (m_phase == Phase::AwaitingHello)
      attemptFailed("connect failed: " + reason);
  }

  // 3. Completed sends that failed mean the link is gone.
  checkSendFailures();

  // 4. Liveness.
  if (m_phase == Phase::Established) {
    const auto quiet = now - lastTraffic();
    if (quiet > m_timings.lossAfterSilence) {
      declareLoss("no traffic from server for "
          + std::to_string(m_timings.lossAfterSilence.count()) + " ms");
    } else if (quiet > m_timings.pingAfterQuiet
        && m_pingSentAt < lastTraffic()) {
      // One Ping per quiet period: the next waits for traffic to come back.
      m_pingSentAt = now;
      sendMessage(encode(Ping{}));
    }
  } else if (m_phase == Phase::AwaitingHello
      && now - m_attemptStart > m_timings.lossAfterSilence) {
    attemptFailed("no Hello from server");
  }

  // 5. Reconnect backoff while Lost.
  if (m_state == ConnectionState::Lost && m_phase == Phase::Idle
      && m_autoRetryEnabled) {
    if (now - m_lostAt > m_timings.autoRetryFor) {
      m_autoRetryEnabled = false;
      m_status = "connection lost; automatic retry gave up";
    } else if (now >= m_nextRetryAt) {
      beginAttempt();
    }
  }
}

// Outbound ///////////////////////////////////////////////////////////////////

void ServerConnection::setFrameConfig(uint32_t width, uint32_t height)
{
  SetFrameConfig config;
  config.width = width;
  config.height = height;
  send(config);
}

void ServerConnection::setEncodings(const std::vector<FrameEncoding> &preferred)
{
  SetEncodings encodings;
  encodings.supported = preferred;
  send(encodings);
}

void ServerConnection::startRendering()
{
  send(StartRendering{});
}

void ServerConnection::stopRendering()
{
  send(StopRendering{});
}

void ServerConnection::setTime(const ShotID &shotId, int frame)
{
  SetTime time;
  time.shotId = shotId;
  time.frame = frame;
  send(time);
}

void ServerConnection::setOutline(
    const std::optional<SceneObjectRef> &objectIdentity)
{
  SetOutline outline;
  outline.objectIdentity = objectIdentity;
  send(outline);
}

void ServerConnection::setViewportSettings(const ViewportSettings &settings)
{
  send(settings);
}

void ServerConnection::sendMessage(vsr::network::Message &&msg)
{
  trySend(std::move(msg));
}

bool ServerConnection::trySend(vsr::network::Message &&msg)
{
  if (m_phase != Phase::Established)
    return false;
  m_sendFutures.push_back(m_channel->send(std::move(msg)));
  return true;
}

void ServerConnection::replyError(const std::string &text)
{
  vsr::core::logError("[ServerConnection] %s", text.c_str());
  Error error;
  error.message = text;
  // Straight to the channel: the rejection goes out in any phase the socket
  // is open, not only once Established.
  m_channel->send(encode(error));
}

void ServerConnection::checkSendFailures()
{
  boost::system::error_code failure;
  auto ready = std::remove_if(m_sendFutures.begin(),
      m_sendFutures.end(),
      [&](vsr::network::MessageFuture &f) {
        if (!vsr::network::is_ready(f))
          return false;
        const auto error = f.valid() ? f.get() : boost::system::error_code{};
        if (error && !failure)
          failure = error;
        return true;
      });
  m_sendFutures.erase(ready, m_sendFutures.end());
  if (failure && m_phase == Phase::Established)
    declareLoss("send failed: " + failure.message());
}

// IO thread //////////////////////////////////////////////////////////////////

void ServerConnection::onInbound(const vsr::network::Message &msg)
{
  markTraffic();
  switch (StudioMessageType(msg.header.type)) {
  case StudioMessageType::Ping:
    m_channel->send(encode(Pong{}));
    return;
  case StudioMessageType::Frame: {
    std::lock_guard lock(m_inboundMutex);
    m_latestFrame = msg;
    return;
  }
  default: {
    std::lock_guard lock(m_inboundMutex);
    m_inbound.push_back(msg);
    return;
  }
  }
}

void ServerConnection::onChannelClosed(const boost::system::error_code &error)
{
  {
    std::lock_guard lock(m_inboundMutex);
    m_ioDisconnectError = error;
  }
  m_ioDisconnected.store(true);
}

void ServerConnection::markTraffic()
{
  m_lastTraffic.store(Clock::now().time_since_epoch().count());
}

ServerConnection::Clock::time_point ServerConnection::lastTraffic() const
{
  return Clock::time_point(Clock::duration(m_lastTraffic.load()));
}

// Connection lifecycle (UI thread) ///////////////////////////////////////////

void ServerConnection::beginAttempt()
{
  m_phase = Phase::AwaitingHello;
  m_attemptStart = Clock::now();
  m_pingSentAt = {};
  m_sendFutures.clear();
  {
    std::lock_guard lock(m_inboundMutex);
    m_inbound.clear();
  }
  markTraffic();
  m_status =
      (m_state == ConnectionState::Lost ? "reconnecting to " : "connecting to ")
      + endpointText(m_host, m_port) + "...";
  // Arm the latch before the socket exists; resolution and connect run on
  // the IO thread and report through the disconnect handler either way.
  m_ioDisconnected.store(false);
  m_channel->connect(m_host, m_port);
}

void ServerConnection::closeChannel()
{
  m_bootstrapped = false;
  setDelegateEnabled(false);
  // Fires our disconnect handler on this thread if the socket was open; that
  // report describes a close we asked for, so it is consumed here.
  m_channel->disconnect();
  m_ioDisconnected.store(false);
  m_sendFutures.clear();
}

void ServerConnection::setState(ConnectionState to)
{
  if (to == m_state)
    return;
  const auto from = m_state;
  m_state = to;
  vsr::core::logStatus(
      "[ServerConnection] %s -> %s", toString(from), toString(to));
  if (onStateChanged)
    onStateChanged(from, to);
}

bool ServerConnection::canEmitEdits() const
{
  return m_phase == Phase::Established && m_bootstrapped && !m_bootstrapping;
}

void ServerConnection::setDelegateEnabled(bool enabled)
{
  if (m_delegate)
    m_delegate->setEnabled(enabled);
}

void ServerConnection::declareLoss(const std::string &reason)
{
  const auto now = Clock::now();
  std::string why = reason;
  if (!m_farewellReason.empty()) {
    // The server said why before it closed.
    why = m_farewellReason;
    m_farewellReason.clear();
  }
  vsr::core::logWarning("[ServerConnection] connection lost: %s", why.c_str());
  m_phase = Phase::Idle;
  if (m_bootstrapping) {
    // The bootstrap was cut short: the mirror holds whatever part of the
    // new scene arrived, which is nothing to show. Empty beats half-built;
    // the replica is still the previous session's (its snapshot comes last
    // in the bracket) and stays as display data.
    m_bootstrapping = false;
    announceMirrorReplace();
    clearMirror();
  }
  closeChannel();
  // Connection-scoped request failure: nothing waits on a reply that can no
  // longer come.
  m_projectOps->failAllPending("connection lost");
  m_lostAt = now;
  m_retryDelay = m_timings.retryInitialDelay;
  m_nextRetryAt = now + m_retryDelay;
  m_autoRetryEnabled = true;
  m_status = "connection lost (" + why + "); reconnecting...";
  setState(ConnectionState::Lost);
}

void ServerConnection::attemptFailed(const std::string &reason)
{
  vsr::core::logWarning(
      "[ServerConnection] attempt failed: %s", reason.c_str());
  m_phase = Phase::Idle;
  closeChannel();
  if (m_state == ConnectionState::Lost && m_autoRetryEnabled) {
    scheduleRetry();
    m_status = "connection lost (" + reason + "); reconnecting...";
  } else {
    m_status = reason;
  }
}

void ServerConnection::scheduleRetry()
{
  m_nextRetryAt = Clock::now() + m_retryDelay;
  m_retryDelay = std::min(m_retryDelay * 2, m_timings.retryMaxDelay);
}

// Inbound handling (UI thread) ///////////////////////////////////////////////

void ServerConnection::handleMessage(const vsr::network::Message &msg)
{
  const auto type = messageType(msg);
  if (!type) {
    replyError("unknown message type " + std::to_string(int(msg.header.type)));
    return;
  }

  if (m_phase == Phase::AwaitingHello) {
    if (*type == StudioMessageType::Hello) {
      handleHello(msg);
    } else if (*type == StudioMessageType::Error) {
      const auto error = decode<Error>(msg);
      attemptFailed(
          "server refused: " + (error ? error->message : std::string("?")));
    } else if (*type == StudioMessageType::Disconnect) {
      attemptFailed(farewellReason(decode<Disconnect>(msg)));
    } else {
      vsr::core::logError(
          "[ServerConnection] %s received before the server's Hello",
          toString(*type));
    }
    return;
  }

  if (isSceneMessageType(*type)) {
    applySceneMessage(*type, msg);
    return;
  }

  switch (*type) {
  case StudioMessageType::Hello:
    vsr::core::logWarning(
        "[ServerConnection] unexpected Hello on an established connection");
    return;
  case StudioMessageType::Pong:
    return; // traffic already stamped on the IO thread
  case StudioMessageType::Error: {
    const auto error = decode<Error>(msg);
    if (!error) {
      vsr::core::logError("[ServerConnection] undecodable Error payload");
      return;
    }
    // A refusal that names a request type is that request's answer (the
    // server could not read its id); anything else is for the banner.
    if (m_projectOps->failOldestNamed(error->message))
      return;
    vsr::core::logError(
        "[ServerConnection] server error: %s", error->message.c_str());
    if (onServerError)
      onServerError(error->message);
    return;
  }
  case StudioMessageType::Disconnect:
    // The server's farewell: the close that follows is explained by it.
    m_farewellReason = farewellReason(decode<Disconnect>(msg));
    vsr::core::logStatus("[ServerConnection] server closing the session: %s",
        m_farewellReason.c_str());
    return;
  case StudioMessageType::BootstrapBegin:
    m_bootstrapping = true;
    m_bootstrapped = false;
    setDelegateEnabled(false);
    announceMirrorReplace();
    clearMirror();
    // Task records: the server dropped the old session's queue without a
    // word, and a restarted server mints ids from 1 again, so every open
    // record is failed here; the task-status replay inside the bracket
    // revives the ones the server finished or is still running.
    m_projectOps->failUnfinishedTasks("connection lost");
    return;
  case StudioMessageType::BootstrapEnd:
    m_bootstrapping = false;
    m_bootstrapped = true;
    setDelegateEnabled(canEmitEdits());
    if (onBootstrapComplete)
      onBootstrapComplete();
    return;
  case StudioMessageType::FrameConfig: {
    const auto config = decode<FrameConfig>(msg);
    if (!config) {
      vsr::core::logError("[ServerConnection] undecodable FrameConfig");
      return;
    }
    m_frameConfig = *config;
    return;
  }
  case StudioMessageType::ProjectSnapshot: {
    auto snapshot = decode<ProjectSnapshot>(msg);
    if (!snapshot) {
      vsr::core::logError("[ServerConnection] undecodable ProjectSnapshot");
      return;
    }
    m_project = std::make_unique<Project>(std::move(snapshot->project));
    if (onProjectReplaced)
      onProjectReplaced();
    return;
  }
  case StudioMessageType::ProjectOpReply: {
    const auto reply = decode<ProjectOpReply>(msg);
    if (!reply) {
      vsr::core::logError("[ServerConnection] undecodable ProjectOpReply");
      return;
    }
    m_projectOps->handleReply(*reply);
    return;
  }
  case StudioMessageType::TaskProgress: {
    const auto progress = decode<TaskProgress>(msg);
    if (!progress) {
      vsr::core::logError("[ServerConnection] undecodable TaskProgress");
      return;
    }
    m_projectOps->handleTaskProgress(*progress);
    return;
  }
  case StudioMessageType::TaskCompleted: {
    const auto completed = decode<TaskCompleted>(msg);
    if (!completed) {
      vsr::core::logError("[ServerConnection] undecodable TaskCompleted");
      return;
    }
    m_projectOps->handleTaskCompleted(*completed);
    return;
  }
  case StudioMessageType::TaskFailed: {
    const auto failed = decode<TaskFailed>(msg);
    if (!failed) {
      vsr::core::logError("[ServerConnection] undecodable TaskFailed");
      return;
    }
    m_projectOps->handleTaskFailed(*failed);
    return;
  }
  case StudioMessageType::TimeAdvanceWarning: {
    const auto warning = decode<TimeAdvanceWarning>(msg);
    if (!warning) {
      vsr::core::logError("[ServerConnection] undecodable TimeAdvanceWarning");
      return;
    }
    vsr::core::logWarning(
        "[ServerConnection] time advance warning (%s @ %d):"
        " %s",
        warning->shotId.c_str(),
        warning->frame,
        warning->message.c_str());
    m_timeAdvanceWarning = *warning;
    if (onTimeAdvanceWarning)
      onTimeAdvanceWarning(*warning);
    return;
  }
  case StudioMessageType::PickReply: {
    const auto reply = decode<PickReply>(msg);
    if (!reply) {
      vsr::core::logError("[ServerConnection] undecodable PickReply");
      return;
    }
    m_projectOps->handlePickReply(*reply);
    return;
  }
  case StudioMessageType::UIState: {
    const auto state = decode<UIState>(msg);
    if (!state) {
      vsr::core::logError("[ServerConnection] undecodable UIState");
      return;
    }
    m_uiState = state->tree;
    if (onUIState)
      onUIState(m_uiState);
    return;
  }
  default:
    vsr::core::logWarning(
        "[ServerConnection] %s is not handled by this client yet",
        toString(*type));
    return;
  }
}

void ServerConnection::handleHello(const vsr::network::Message &msg)
{
  const auto hello = decode<Hello>(msg);
  if (!hello) {
    attemptFailed("undecodable Hello from server");
    return;
  }
  if (hello->version != PROTOCOL_VERSION) {
    // A version mismatch will not heal by retrying.
    const std::string mismatch = "protocol version mismatch: server speaks v"
        + std::to_string(hello->version) + ", this client v"
        + std::to_string(PROTOCOL_VERSION);
    vsr::core::logError("[ServerConnection] %s", mismatch.c_str());
    if (m_state == ConnectionState::Lost) {
      // The server that came back is not one this client can talk to: the
      // frozen view has no future, so this is a completed disconnect, not a
      // loss the banner should keep offering to retry.
      dropSession(mismatch);
      return;
    }
    m_phase = Phase::Idle;
    closeChannel();
    m_autoRetryEnabled = false;
    m_status = mismatch;
    return;
  }

  m_phase = Phase::Established;
  Hello reply;
  reply.version = PROTOCOL_VERSION;
  sendMessage(encode(reply));
  m_status = "connected to " + endpointText(m_host, m_port);
  if (!hello->buildInfo.empty())
    m_status += " (" + hello->buildInfo + ")";
  // The delegate stays off until BootstrapEnd: between here and the bootstrap
  // the mirror is a frozen view of the previous session (or empty) and an
  // edit to it must not reach the new server.
  setState(ConnectionState::Connected);
}

void ServerConnection::applySceneMessage(
    StudioMessageType type, const vsr::network::Message &msg)
{
  if (!m_mirror)
    return;
  // Origin-based echo suppression: what the server pushes must not be sent
  // back as an optimistic edit.
  setDelegateEnabled(false);
  switch (type) {
  case StudioMessageType::TransferScene:
    // A whole-scene push outside the bootstrap (the server re-sent its scene)
    // replaces every object too; inside one BootstrapBegin already announced
    // it and the mirror is empty.
    if (!m_bootstrapping)
      announceMirrorReplace();
    messages::TransferScene(msg, m_mirror).execute();
    break;
  case StudioMessageType::TransferLayer:
    messages::TransferLayer(msg, m_mirror).execute();
    break;
  case StudioMessageType::ObjectAdded:
    messages::NewObject(msg, m_mirror).execute();
    break;
  case StudioMessageType::ObjectRemoved:
    messages::RemoveObject(msg, m_mirror).execute();
    break;
  default:
    break;
  }
  setDelegateEnabled(canEmitEdits());
}

void ServerConnection::announceMirrorReplace()
{
  if (onMirrorReplaceBegin)
    onMirrorReplaceBegin();
}

void ServerConnection::clearMirror()
{
  if (!m_mirror)
    return;
  setDelegateEnabled(false);
  m_mirror->removeAllObjects();
  setDelegateEnabled(canEmitEdits());
}

} // namespace vsr::scivis_studio::client
