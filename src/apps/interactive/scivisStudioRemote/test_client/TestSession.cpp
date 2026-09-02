// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "ProjectSnapshot.h"
#include "SceneEditMessages.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_network
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <thread>

namespace vsr::scivis_studio::test_client {

using namespace protocol;
using vsr::network::Message;
namespace messages = vsr::network::messages;

namespace {

// Bound on flushing a courtesy Disconnect before the socket closes.
constexpr std::chrono::milliseconds COURTESY_SEND_TIMEOUT{200};
constexpr std::chrono::milliseconds POLL_INTERVAL{1};
constexpr const char *BUILD_INFO = "scivisStudioTestClient";

size_t totalObjects(const vsr::scene::Scene &scene)
{
  size_t n = 0;
  for (auto type : {ANARI_ARRAY,
           ANARI_SURFACE,
           ANARI_GEOMETRY,
           ANARI_MATERIAL,
           ANARI_SAMPLER,
           ANARI_VOLUME,
           ANARI_SPATIAL_FIELD,
           ANARI_LIGHT,
           ANARI_CAMERA,
           ANARI_RENDERER})
    n += scene.numberOfObjects(type);
  return n;
}

std::string endpointText(const std::string &host, int port)
{
  return host + ":" + std::to_string(port);
}

std::string objectText(const SceneObjectRef &ref)
{
  return std::string(anari::toString(ref.type)) + " "
      + std::to_string(ref.objectIndex);
}

} // namespace

const char *toString(SessionState state)
{
  switch (state) {
  case SessionState::NeverConnected:
    return "NeverConnected";
  case SessionState::Connected:
    return "Connected";
  case SessionState::Lost:
    return "Lost";
  case SessionState::Disconnected:
    return "Disconnected";
  }
  return "Unknown";
}

std::string Event::text() const
{
  std::string out = name;
  for (const auto &[key, value] : fields) {
    out += ' ';
    out += key;
    out += '=';
    out += value;
  }
  return out;
}

// Construction ///////////////////////////////////////////////////////////////

TestSession::TestSession(SessionTimings timings)
    : m_timings(timings),
      m_channel(std::make_shared<vsr::network::NetworkClient>())
{
  // Registered before any connect() so the IO thread never sees the handler
  // map change. Every type byte gets one: a message outside the Studio set
  // is answered with an Error, not dropped by the transport.
  for (int value = 0; value <= 0xff; ++value) {
    m_channel->registerHandler(
        uint8_t(value), [this](const Message &msg) { onInbound(msg); });
  }
  m_channel->setDisconnectHandler(
      [this](
          const boost::system::error_code &error) { onChannelClosed(error); });
}

TestSession::~TestSession()
{
  // Joining the IO thread first guarantees no handler runs against a
  // half-destroyed object.
  m_channel->disconnect();
  m_channel.reset();
}

// Queries ////////////////////////////////////////////////////////////////////

SessionState TestSession::state() const
{
  return m_state;
}

const std::string &TestSession::host() const
{
  return m_host;
}

int TestSession::port() const
{
  return m_port;
}

vsr::scene::Scene &TestSession::mirror()
{
  return m_mirror;
}

const vsr::scene::Scene &TestSession::mirror() const
{
  return m_mirror;
}

const Project *TestSession::project() const
{
  return m_project.get();
}

const FrameConfig &TestSession::frameConfig() const
{
  return m_frameConfig;
}

const std::optional<FrameHeader> &TestSession::lastFrameHeader() const
{
  return m_lastFrameHeader;
}

const Message &TestSession::lastFrame() const
{
  return m_lastFrame;
}

size_t TestSession::framesReceived() const
{
  return m_framesReceived;
}

size_t TestSession::errorsReceived() const
{
  return m_errorsReceived;
}

const std::string &TestSession::lastError() const
{
  return m_lastError;
}

// Session ////////////////////////////////////////////////////////////////////

bool TestSession::connect(const std::string &host,
    int port,
    std::chrono::milliseconds deadline,
    std::string *error)
{
  if (m_phase != Phase::Idle) {
    m_phase = Phase::Idle;
    closeChannel();
  }
  m_host = host;
  m_port = port;
  beginAttempt();

  const auto end = Clock::now() + deadline;
  while (true) {
    poll();
    if (m_phase == Phase::Established && m_bootstrapped)
      return true;
    if (m_phase == Phase::Idle) {
      if (error)
        *error = m_failure;
      return false;
    }
    if (Clock::now() >= end)
      break;
    std::this_thread::sleep_for(POLL_INTERVAL);
  }

  m_failure = (m_phase == Phase::AwaitingHello ? "no Hello from "
                                               : "no complete Bootstrap from ")
      + endpointText(host, port) + " within " + std::to_string(deadline.count())
      + " ms";
  m_phase = Phase::Idle;
  closeChannel();
  if (error)
    *error = m_failure;
  return false;
}

bool TestSession::reconnect(
    std::chrono::milliseconds deadline, std::string *error)
{
  if (m_host.empty()) {
    if (error)
      *error = "never connected: nothing to reconnect to";
    return false;
  }
  return connect(m_host, m_port, deadline, error);
}

void TestSession::disconnect()
{
  if (m_phase == Phase::Established) {
    auto sent = m_channel->send(encode(Disconnect{}));
    sent.wait_for(COURTESY_SEND_TIMEOUT);
  }
  m_phase = Phase::Idle;
  closeChannel();
  finishDisconnect();
}

bool TestSession::shutdown(
    std::chrono::milliseconds deadline, std::string *error)
{
  if (!requireConnected(error))
    return false;
  send(encode(Shutdown{}));
  m_phase = Phase::Closing;

  const auto end = Clock::now() + deadline;
  while (m_phase == Phase::Closing && Clock::now() < end) {
    poll();
    std::this_thread::sleep_for(POLL_INTERVAL);
  }
  poll();
  if (m_phase != Phase::Closing)
    return true;

  // The server kept the socket open; we still meant to leave.
  m_phase = Phase::Idle;
  closeChannel();
  finishDisconnect();
  if (error) {
    *error = "server did not close the socket within "
        + std::to_string(deadline.count()) + " ms of Shutdown";
  }
  return false;
}

// Pumping ////////////////////////////////////////////////////////////////////

void TestSession::poll()
{
  const auto now = Clock::now();

  // 1. The IO thread's disconnect latch.
  if (m_ioDisconnected.exchange(false)) {
    boost::system::error_code error;
    {
      std::lock_guard lock(m_inboundMutex);
      error = m_ioDisconnectError;
    }
    const std::string reason = error ? error.message() : "connection closed";
    switch (m_phase) {
    case Phase::Established:
      declareLoss(reason);
      break;
    case Phase::AwaitingHello:
      attemptFailed("connect failed: " + reason);
      break;
    case Phase::Closing:
      // The close we asked for with Shutdown.
      m_phase = Phase::Idle;
      closeChannel();
      finishDisconnect();
      break;
    case Phase::Idle:
      break;
    }
  }

  // 2. Inbound messages, on this thread only.
  std::vector<Message> batch;
  {
    std::lock_guard lock(m_inboundMutex);
    batch.swap(m_inbound);
  }
  for (const auto &msg : batch) {
    if (m_phase == Phase::Idle)
      break; // a handled message tore the connection down; drop the rest
    handleMessage(msg);
  }
  if (m_phase != Phase::Idle)
    consumeFrame();

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
      send(encode(Ping{}));
    }
  }
}

bool TestSession::takeEvent(Event &out)
{
  if (m_events.empty())
    return false;
  out = std::move(m_events.front());
  m_events.pop_front();
  return true;
}

bool TestSession::pollUntil(
    const std::function<bool()> &done, std::chrono::milliseconds deadline)
{
  const auto end = Clock::now() + deadline;
  while (true) {
    poll();
    if (done())
      return true;
    if (Clock::now() >= end)
      return false;
    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

// Outbound ///////////////////////////////////////////////////////////////////

bool TestSession::send(Message &&msg, std::string *error)
{
  if (!requireConnected(error))
    return false;
  m_sendFutures.push_back(m_channel->send(std::move(msg)));
  return true;
}

bool TestSession::sendRaw(
    uint8_t type, std::vector<std::byte> payload, std::string *error)
{
  Message msg;
  msg.header.type = type;
  msg.header.payload_length = uint32_t(payload.size());
  msg.payload = std::move(payload);
  return send(std::move(msg), error);
}

bool TestSession::ping(std::string *error)
{
  return send(Ping{}, error);
}

bool TestSession::setFrameConfig(
    uint32_t width, uint32_t height, std::string *error)
{
  SetFrameConfig config;
  config.width = width;
  config.height = height;
  return send(config, error);
}

bool TestSession::setEncodings(
    const std::vector<FrameEncoding> &preferred, std::string *error)
{
  SetEncodings encodings;
  encodings.supported = preferred;
  return send(encodings, error);
}

bool TestSession::startRendering(std::string *error)
{
  return send(StartRendering{}, error);
}

bool TestSession::stopRendering(std::string *error)
{
  return send(StopRendering{}, error);
}

bool TestSession::setParameter(const SceneObjectRef &object,
    const std::string &name,
    const vsr::core::Any &value,
    std::string *error)
{
  if (!requireConnected(error))
    return false;
  auto *obj = m_mirror.getObject(object.type, object.objectIndex);
  if (!obj) {
    if (error)
      *error = "no " + objectText(object) + " in the mirror";
    return false;
  }
  obj->addParameter(name).setValue(value);
  SetObjectParameter edit;
  edit.object = object;
  edit.name = name;
  edit.value = value;
  return send(edit, error);
}

bool TestSession::removeParameter(
    const SceneObjectRef &object, const std::string &name, std::string *error)
{
  if (!requireConnected(error))
    return false;
  auto *obj = m_mirror.getObject(object.type, object.objectIndex);
  if (!obj) {
    if (error)
      *error = "no " + objectText(object) + " in the mirror";
    return false;
  }
  obj->removeParameter(name);
  RemoveObjectParameter edit;
  edit.object = object;
  edit.name = name;
  return send(edit, error);
}

bool TestSession::setNodeTransform(const SceneNodeRef &node,
    const vsr::math::mat4 &transform,
    std::string *error)
{
  if (!requireConnected(error))
    return false;
  auto *layer = m_mirror.layer(node.layerName.c_str());
  if (!layer) {
    if (error)
      *error = "no layer '" + node.layerName + "' in the mirror";
    return false;
  }
  // Node indices are the server's: TransferLayer rebuilds the mirror's
  // forest in traversal order, so a sparse server layer numbers its nodes
  // differently. The mirror is updated when it agrees; the server is the
  // truth either way.
  auto ref = layer->at(node.nodeIndex);
  if (ref && (*ref)->isTransform()) {
    (*ref)->setAsTransform(transform);
    m_mirror.signalLayerTransformChanged(layer);
  } else {
    vsr::core::logWarning(
        "[TestSession] mirror has no transform node '%s'[%zu]; edit sent"
        " unmirrored",
        node.layerName.c_str(),
        node.nodeIndex);
  }
  SetNodeTransform edit;
  edit.node = node;
  edit.transform = transform;
  return send(edit, error);
}

// IO thread //////////////////////////////////////////////////////////////////

void TestSession::onInbound(const Message &msg)
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

void TestSession::onChannelClosed(const boost::system::error_code &error)
{
  {
    std::lock_guard lock(m_inboundMutex);
    m_ioDisconnectError = error;
  }
  m_ioDisconnected.store(true);
}

void TestSession::markTraffic()
{
  m_lastTraffic.store(Clock::now().time_since_epoch().count());
}

TestSession::Clock::time_point TestSession::lastTraffic() const
{
  return Clock::time_point(Clock::duration(m_lastTraffic.load()));
}

// Connection lifecycle (caller's thread) /////////////////////////////////////

void TestSession::beginAttempt()
{
  m_phase = Phase::AwaitingHello;
  m_pingSentAt = {};
  m_failure.clear();
  m_sendFutures.clear();
  {
    std::lock_guard lock(m_inboundMutex);
    m_inbound.clear();
    m_latestFrame.reset();
  }
  markTraffic();
  // Armed before the socket exists: resolution and connect run on the IO
  // thread and report through the disconnect handler either way.
  m_ioDisconnected.store(false);
  m_channel->connect(m_host, short(m_port));
}

void TestSession::closeChannel()
{
  m_bootstrapping = false;
  m_bootstrapped = false;
  // Fires our disconnect handler on this thread if the socket was open; that
  // report describes a close we asked for, so it is consumed here.
  m_channel->disconnect();
  m_ioDisconnected.store(false);
  m_sendFutures.clear();
}

void TestSession::setState(SessionState to)
{
  if (to == m_state)
    return;
  vsr::core::logStatus(
      "[TestSession] %s -> %s", toString(m_state), toString(to));
  m_state = to;
}

void TestSession::declareLoss(const std::string &reason)
{
  vsr::core::logWarning("[TestSession] connection lost: %s", reason.c_str());
  m_failure = reason;
  m_phase = Phase::Idle;
  closeChannel();
  setState(SessionState::Lost);
}

void TestSession::attemptFailed(const std::string &reason)
{
  vsr::core::logWarning("[TestSession] attempt failed: %s", reason.c_str());
  m_failure = reason;
  m_phase = Phase::Idle;
  closeChannel();
}

void TestSession::finishDisconnect()
{
  clearMirror();
  m_project.reset();
  m_frameConfig = {};
  if (m_state != SessionState::NeverConnected)
    setState(SessionState::Disconnected);
}

void TestSession::clearMirror()
{
  m_mirror.removeAllObjects();
  m_mirror.removeAllLayers();
}

void TestSession::checkSendFailures()
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

bool TestSession::requireConnected(std::string *error) const
{
  if (m_phase == Phase::Established)
    return true;
  if (error)
    *error = std::string("not connected (") + toString(m_state) + ")";
  return false;
}

void TestSession::pushEvent(Event event)
{
  m_events.push_back(std::move(event));
}

void TestSession::replyError(const std::string &text)
{
  vsr::core::logError("[TestSession] %s", text.c_str());
  Error error;
  error.message = text;
  m_channel->send(encode(error));
}

// Inbound handling (caller's thread) /////////////////////////////////////////

void TestSession::handleMessage(const Message &msg)
{
  const auto type = messageType(msg);
  if (!type) {
    Event event{"Unknown", {}};
    event.fields.emplace_back("type", std::to_string(int(msg.header.type)));
    pushEvent(std::move(event));
    replyError("unknown message type " + std::to_string(int(msg.header.type)));
    return;
  }

  Event event{toString(*type), {}};

  if (m_phase == Phase::AwaitingHello) {
    if (*type == StudioMessageType::Hello) {
      handleHello(msg);
    } else if (*type == StudioMessageType::Error) {
      const auto error = decode<Error>(msg);
      const std::string text = error ? error->message : "?";
      event.fields.emplace_back("message", "\"" + text + "\"");
      pushEvent(std::move(event));
      attemptFailed("server refused: " + text);
    } else {
      pushEvent(std::move(event));
      vsr::core::logError("[TestSession] %s received before the server's Hello",
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
        "[TestSession] unexpected Hello on an established connection");
    break;
  case StudioMessageType::Pong:
    break;
  case StudioMessageType::Error: {
    const auto error = decode<Error>(msg);
    m_lastError = error ? error->message : "(undecodable Error)";
    ++m_errorsReceived;
    event.fields.emplace_back("message", "\"" + m_lastError + "\"");
    vsr::core::logWarning(
        "[TestSession] server error: %s", m_lastError.c_str());
    break;
  }
  case StudioMessageType::BootstrapBegin:
    m_bootstrapping = true;
    m_bootstrapped = false;
    clearMirror();
    break;
  case StudioMessageType::BootstrapEnd:
    m_bootstrapping = false;
    m_bootstrapped = true;
    break;
  case StudioMessageType::FrameConfig: {
    const auto config = decode<FrameConfig>(msg);
    if (!config) {
      vsr::core::logError("[TestSession] undecodable FrameConfig");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    m_frameConfig = *config;
    event.fields.emplace_back("width", std::to_string(config->width));
    event.fields.emplace_back("height", std::to_string(config->height));
    break;
  }
  case StudioMessageType::ProjectSnapshot: {
    auto snapshot = decode<ProjectSnapshot>(msg);
    if (!snapshot) {
      vsr::core::logError("[TestSession] undecodable ProjectSnapshot");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    m_project = std::make_unique<Project>(std::move(snapshot->project));
    event.fields.emplace_back("activeShot", m_project->activeShotId);
    event.fields.emplace_back("shots", std::to_string(m_project->shots.size()));
    event.fields.emplace_back(
        "datasets", std::to_string(m_project->datasets.size()));
    break;
  }
  default:
    vsr::core::logWarning(
        "[TestSession] %s is not handled by this client", toString(*type));
    break;
  }
  pushEvent(std::move(event));
}

void TestSession::handleHello(const Message &msg)
{
  const auto hello = decode<Hello>(msg);
  if (!hello) {
    attemptFailed("undecodable Hello from server");
    return;
  }
  Event event{"Hello", {}};
  event.fields.emplace_back("version", std::to_string(hello->version));
  event.fields.emplace_back("buildInfo", "\"" + hello->buildInfo + "\"");
  pushEvent(std::move(event));

  if (hello->version != PROTOCOL_VERSION) {
    attemptFailed("protocol version mismatch: server speaks v"
        + std::to_string(hello->version) + ", this client v"
        + std::to_string(PROTOCOL_VERSION));
    return;
  }

  m_phase = Phase::Established;
  Hello reply;
  reply.version = PROTOCOL_VERSION;
  reply.buildInfo = BUILD_INFO;
  send(encode(reply));
  setState(SessionState::Connected);
}

void TestSession::applySceneMessage(StudioMessageType type, const Message &msg)
{
  Event event{toString(type), {}};
  switch (type) {
  case StudioMessageType::TransferScene:
    messages::TransferScene(msg, &m_mirror).execute();
    break;
  case StudioMessageType::TransferLayer:
    messages::TransferLayer(msg, &m_mirror).execute();
    break;
  case StudioMessageType::ObjectAdded:
    messages::NewObject(msg, &m_mirror).execute();
    break;
  case StudioMessageType::ObjectRemoved:
    messages::RemoveObject(msg, &m_mirror).execute();
    break;
  default:
    break;
  }
  event.fields.emplace_back("objects", std::to_string(totalObjects(m_mirror)));
  event.fields.emplace_back(
      "layers", std::to_string(m_mirror.numberOfLayers()));
  pushEvent(std::move(event));
}

void TestSession::consumeFrame()
{
  std::optional<Message> frame;
  {
    std::lock_guard lock(m_inboundMutex);
    frame.swap(m_latestFrame);
  }
  if (!frame)
    return;

  Event event{"Frame", {}};
  const auto view = decodeFrame(*frame);
  if (!view) {
    vsr::core::logError("[TestSession] malformed Frame dropped");
    event.fields.emplace_back("malformed", "true");
    pushEvent(std::move(event));
    return;
  }
  const auto &h = view->header;
  event.fields.emplace_back("width", std::to_string(h.width));
  event.fields.emplace_back("height", std::to_string(h.height));
  event.fields.emplace_back("encoding", toString(h.encoding));
  event.fields.emplace_back("pixelFormat", toString(h.pixelFormat));
  event.fields.emplace_back("shotId", h.shotId);
  event.fields.emplace_back("frame", std::to_string(h.frame));
  event.fields.emplace_back("bytes", std::to_string(view->size));
  m_lastFrameHeader = h;
  m_lastFrame = std::move(*frame);
  ++m_framesReceived;
  pushEvent(std::move(event));
}

} // namespace vsr::scivis_studio::test_client
