// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
#include "ProjectSnapshot.h"
#include "SceneEditMessages.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
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
#include <cctype>
#include <charconv>
#include <thread>

namespace vsr::scivis_studio::test_client {

using namespace protocol;
using vsr::network::Message;
namespace messages = vsr::network::messages;

namespace {

// Bound on flushing a courtesy Disconnect before the socket closes.
constexpr std::chrono::milliseconds COURTESY_SEND_TIMEOUT{200};
constexpr std::chrono::milliseconds POLL_INTERVAL{1};
// Between reconnect attempts a restarting server refuses.
constexpr std::chrono::milliseconds RECONNECT_PAUSE{100};
constexpr const char *BUILD_INFO = "scivisStudioTestClient";
// What a BootstrapBegin says of the tasks the previous session left running.
constexpr const char *CONNECTION_LOST = "connection lost";

std::string endpointText(const std::string &host, int port)
{
  return host + ":" + std::to_string(port);
}

std::string objectText(const SceneObjectRef &ref)
{
  return std::string(anari::toString(ref.type)) + " "
      + std::to_string(ref.objectIndex);
}

std::string quotedText(const std::string &text)
{
  return "\"" + text + "\"";
}

// The shortest text that reads back as the same float.
std::string numberText(float value)
{
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

// "ANARI_SURFACE" -> "surface", the record stream's spelling of a type.
std::string shortTypeName(anari::DataType type)
{
  std::string name = anari::toString(type);
  if (name.rfind("ANARI_", 0) == 0)
    name.erase(0, 6);
  for (auto &c : name)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return name;
}

const char *boolText(bool value)
{
  return value ? "true" : "false";
}

// A TaskFailed's `framesCompleted`, read off the wire tree: a cancelled or
// failed RenderShot reports the frames it left on disk there, and the payload
// struct does not carry the field yet (milestone 7 server work). Absent or
// mistyped reads as 0, like every optional child.
uint64_t failedFramesCompleted(const Message &msg)
{
  vsr::core::DataTree tree;
  if (msg.payload.empty() || !tree.read(msg.payload))
    return 0;
  return readChildOr(tree.root(), "framesCompleted", uint64_t(0));
}

} // namespace

const char *toString(TaskRecord::Status status)
{
  switch (status) {
  case TaskRecord::Status::Queued:
    return "Queued";
  case TaskRecord::Status::Running:
    return "Running";
  case TaskRecord::Status::Completed:
    return "Completed";
  case TaskRecord::Status::Failed:
    return "Failed";
  }
  return "Unknown";
}

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

Event frameEvent(const FrameHeader &header, size_t bytes)
{
  Event event{"Frame", {}};
  event.fields.emplace_back("width", std::to_string(header.width));
  event.fields.emplace_back("height", std::to_string(header.height));
  event.fields.emplace_back("encoding", toString(header.encoding));
  event.fields.emplace_back("pixelFormat", toString(header.pixelFormat));
  event.fields.emplace_back("shotId", header.shotId);
  event.fields.emplace_back("frame", std::to_string(header.frame));
  event.fields.emplace_back("bytes", std::to_string(bytes));
  return event;
}

size_t totalObjects(const vsr::scene::Scene &scene)
{
  size_t n = 0;
  forEachObjectPool(scene.objectDB(),
      [&](anari::DataType, const auto &pool) { n += pool.size(); });
  return n;
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

size_t TestSession::framesAdvanced() const
{
  return m_framesAdvanced;
}

int TestSession::frameMaxStep() const
{
  return m_frameMaxStep;
}

size_t TestSession::errorsReceived() const
{
  return m_errorsReceived;
}

const std::string &TestSession::lastError() const
{
  return m_lastError;
}

const std::string &TestSession::failure() const
{
  return m_failure;
}

uint64_t TestSession::nextRequestId()
{
  return m_nextRequestId++;
}

const ProjectOpReply *TestSession::reply(uint64_t requestId) const
{
  const auto *received = m_replies.at(requestId);
  return received ? &received->reply : nullptr;
}

std::optional<size_t> TestSession::snapshotsAtReply(uint64_t requestId) const
{
  const auto *received = m_replies.at(requestId);
  if (!received)
    return {};
  return received->snapshotsReceived;
}

size_t TestSession::repliesFailed() const
{
  return m_repliesFailed;
}

const TaskRecord *TestSession::task(uint64_t taskId) const
{
  return m_tasks.at(taskId);
}

size_t TestSession::tasksCompleted() const
{
  return m_tasksCompleted;
}

size_t TestSession::tasksFailed() const
{
  return m_tasksFailed;
}

size_t TestSession::tasksReplayed() const
{
  return m_tasksReplayed;
}

size_t TestSession::snapshotsReceived() const
{
  return m_snapshotsReceived;
}

const std::string &TestSession::lastReplyError() const
{
  return m_lastReplyError;
}

const PickReply *TestSession::pickReply(uint64_t requestId) const
{
  return m_pickReplies.at(requestId);
}

size_t TestSession::warningsReceived() const
{
  return m_warningsReceived;
}

const std::optional<TimeAdvanceWarning> &TestSession::lastWarning() const
{
  return m_lastWarning;
}

const SubtreePtr &TestSession::uiState() const
{
  return m_uiState;
}

// Session ////////////////////////////////////////////////////////////////////

bool TestSession::connect(const std::string &host,
    int port,
    std::chrono::milliseconds deadline,
    std::string *error)
{
  // Connecting over an open link (or a half-open attempt) is an implicit
  // disconnect first; the state then says so.
  if (m_phase != Phase::Idle)
    disconnect();
  m_host = host;
  m_port = port;
  beginAttempt();

  // Every way an attempt ends lands in Idle, except success.
  pollUntil([&] { return m_bootstrapped || m_phase == Phase::Idle; }, deadline);
  if (m_bootstrapped)
    return true;
  if (m_phase != Phase::Idle) {
    // The deadline: Connected was never entered (that takes BootstrapEnd), so
    // closing leaves the state as it was.
    m_failure =
        (m_phase == Phase::AwaitingHello ? "no Hello from "
                                         : "no complete Bootstrap from ")
        + endpointText(host, port) + " within "
        + std::to_string(deadline.count()) + " ms";
    m_phase = Phase::Idle;
    closeChannel();
  }
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
  // Lost is auto-retrying (CONTEXT.md): a server still coming back refuses
  // at once, so attempts repeat until the deadline, each bounded by what is
  // left of it.
  const auto end = Clock::now() + deadline;
  while (true) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - Clock::now());
    if (connect(m_host, m_port, std::max(left, {}), error))
      return true;
    if (Clock::now() + RECONNECT_PAUSE >= end)
      return false;
    std::this_thread::sleep_for(RECONNECT_PAUSE);
  }
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

  if (pollUntil([&] { return m_phase != Phase::Closing; }, deadline))
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

  // 1. Inbound messages, on this thread only. They come before the disconnect
  // latch so a peer's last words (an Error, then close) are still heard.
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

  // 2. The IO thread's disconnect latch.
  if (m_ioDisconnected.exchange(false)) {
    boost::system::error_code error;
    {
      std::lock_guard lock(m_inboundMutex);
      error = m_ioDisconnectError;
    }
    const std::string reason = error ? error.message() : "connection closed";
    switch (m_phase) {
    case Phase::Established:
      linkFailed(reason);
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

  // 3. Completed sends that failed mean the link is gone.
  checkSendFailures();

  // 4. Liveness.
  if (m_phase == Phase::Established) {
    const auto quiet = now - lastTraffic();
    if (quiet > m_timings.lossAfterSilence) {
      linkFailed("no traffic from server for "
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
  auto *obj = mirrorObject(object, error);
  if (!obj)
    return false;
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
  auto *obj = mirrorObject(object, error);
  if (!obj)
    return false;
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
  // Node indices are the server's, and the layer transfers rebuild the
  // mirror with the same numbering, so a missing node means a stale or bad
  // reference. The server is the truth either way: the edit is still sent.
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

void TestSession::linkFailed(const std::string &reason)
{
  // Before BootstrapEnd the link was never Connected, so losing it is one more
  // failed attempt and the state stays as it was.
  if (m_bootstrapped)
    declareLoss(reason);
  else
    attemptFailed("connect failed: " + reason);
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
  m_replies.clear();
  m_pickReplies.clear();
  m_tasks.clear();
  m_uiState.reset();
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
    linkFailed("send failed: " + failure.message());
}

bool TestSession::requireConnected(std::string *error) const
{
  if (m_phase == Phase::Established)
    return true;
  if (error)
    *error = std::string("not connected (") + toString(m_state) + ")";
  return false;
}

vsr::scene::Object *TestSession::mirrorObject(
    const SceneObjectRef &object, std::string *error)
{
  if (!requireConnected(error))
    return nullptr;
  auto *obj = m_mirror.getObject(object.type, object.objectIndex);
  if (!obj && error)
    *error = "no " + objectText(object) + " in the mirror";
  return obj;
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
      event.fields.emplace_back("message", quotedText(text));
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
    const std::string text = error ? error->message : "(undecodable Error)";
    event.fields.emplace_back("message", quotedText(text));
    if (!m_bootstrapped) {
      // Nothing of ours but the Hello is in flight before BootstrapEnd, so an
      // Error here is the server turning the attempt down (a version it does
      // not speak, say); it usually closes right after.
      pushEvent(std::move(event));
      attemptFailed("server refused: " + text);
      return;
    }
    m_lastError = text;
    ++m_errorsReceived;
    vsr::core::logWarning(
        "[TestSession] server error: %s", m_lastError.c_str());
    break;
  }
  case StudioMessageType::BootstrapBegin:
    m_bootstrapping = true;
    m_bootstrapped = false;
    clearMirror();
    // A task still open here belonged to a session that is over: its end
    // message, if any, went to a closed socket. The replay that follows
    // carries what the server still knows; whatever it does not repeat stays
    // failed. Not a message, so tasks.failed does not count it.
    for (auto &[taskId, record] : m_tasks) {
      if (record.finished())
        continue;
      record.status = TaskRecord::Status::Failed;
      record.message = CONNECTION_LOST;
      record.snapshotsAtEnd = m_snapshotsReceived;
    }
    m_tasksReplayed = 0;
    break;
  case StudioMessageType::BootstrapEnd:
    m_bootstrapping = false;
    m_bootstrapped = true;
    // Connected means fully populated: only now are mirror and replica the
    // server's.
    setState(SessionState::Connected);
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
    ++m_snapshotsReceived;
    event.fields.emplace_back("activeShot", m_project->activeShotId);
    event.fields.emplace_back("shots", std::to_string(m_project->shots.size()));
    event.fields.emplace_back(
        "datasets", std::to_string(m_project->datasets.size()));
    event.fields.emplace_back(
        "lightRigs", std::to_string(m_project->lightRigs.size()));
    event.fields.emplace_back(
        "cameraRigs", std::to_string(m_project->cameraRigs.size()));
    event.fields.emplace_back(
        "colorMaps", std::to_string(m_project->colorMaps.size()));
    event.fields.emplace_back("dirty", boolText(m_project->dirty));
    // Time at Rest, as the snapshot carries it for the active shot.
    if (const auto *shot = project::activeShot(*m_project)) {
      event.fields.emplace_back("playing", boolText(shot->playing));
      event.fields.emplace_back(
          "currentFrame", std::to_string(shot->currentFrame));
    }
    break;
  }
  case StudioMessageType::ProjectOpReply: {
    auto reply = decode<ProjectOpReply>(msg);
    if (!reply) {
      vsr::core::logError("[TestSession] undecodable ProjectOpReply");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("requestId", std::to_string(reply->requestId));
    event.fields.emplace_back("ok", boolText(reply->ok));
    event.fields.emplace_back("error", quotedText(reply->error));
    if (!reply->ok) {
      ++m_repliesFailed;
      m_lastReplyError = reply->error;
    } else if (const auto started = results<TaskStartedResult>(*reply)) {
      // Queued from the launch, so a loss before its first progress still
      // leaves a record to fail.
      taskRecord(started->taskId);
    }
    m_replies[reply->requestId] = {std::move(*reply), m_snapshotsReceived};
    break;
  }
  case StudioMessageType::PickReply: {
    auto reply = decode<PickReply>(msg);
    if (!reply) {
      vsr::core::logError("[TestSession] undecodable PickReply");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("requestId", std::to_string(reply->requestId));
    event.fields.emplace_back("hit", boolText(reply->hit));
    const auto &p = reply->worldPosition;
    event.fields.emplace_back("worldPosition",
        quotedText(
            numberText(p.x) + " " + numberText(p.y) + " " + numberText(p.z)));
    if (reply->objectIdentity) {
      event.fields.emplace_back(
          "objectType", shortTypeName(reply->objectIdentity->type));
      event.fields.emplace_back(
          "objectIndex", std::to_string(reply->objectIdentity->objectIndex));
    } else {
      event.fields.emplace_back("objectType", "none");
      event.fields.emplace_back("objectIndex", "none");
    }
    m_pickReplies[reply->requestId] = std::move(*reply);
    break;
  }
  case StudioMessageType::TimeAdvanceWarning: {
    const auto warning = decode<TimeAdvanceWarning>(msg);
    if (!warning) {
      vsr::core::logError("[TestSession] undecodable TimeAdvanceWarning");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("shotId", warning->shotId);
    event.fields.emplace_back("frame", std::to_string(warning->frame));
    event.fields.emplace_back("message", quotedText(warning->message));
    ++m_warningsReceived;
    m_lastWarning = *warning;
    break;
  }
  case StudioMessageType::TaskProgress: {
    const auto progress = decode<TaskProgress>(msg);
    if (!progress) {
      vsr::core::logError("[TestSession] undecodable TaskProgress");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("taskId", std::to_string(progress->taskId));
    event.fields.emplace_back("current", std::to_string(progress->current));
    event.fields.emplace_back("total", std::to_string(progress->total));
    event.fields.emplace_back("message", quotedText(progress->message));
    // Progress after the end would be the server's mistake; the end stands.
    auto &record = taskRecord(progress->taskId);
    if (!record.finished()) {
      record.status = TaskRecord::Status::Running;
      record.current = progress->current;
      record.total = progress->total;
    }
    if (m_bootstrapping)
      ++m_tasksReplayed;
    break;
  }
  case StudioMessageType::TaskCompleted: {
    const auto completed = decode<TaskCompleted>(msg);
    if (!completed) {
      vsr::core::logError("[TestSession] undecodable TaskCompleted");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("taskId", std::to_string(completed->taskId));
    event.fields.emplace_back("message", quotedText(completed->message));
    if (completed->framesCompleted) {
      event.fields.emplace_back(
          "framesCompleted", std::to_string(completed->framesCompleted));
    }
    handleTaskEnd(completed->taskId,
        TaskRecord::Status::Completed,
        std::move(completed->message),
        completed->framesCompleted);
    break;
  }
  case StudioMessageType::TaskFailed: {
    const auto failed = decode<TaskFailed>(msg);
    if (!failed) {
      vsr::core::logError("[TestSession] undecodable TaskFailed");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("taskId", std::to_string(failed->taskId));
    event.fields.emplace_back("error", quotedText(failed->error));
    const auto framesCompleted = failedFramesCompleted(msg);
    if (framesCompleted) {
      event.fields.emplace_back(
          "framesCompleted", std::to_string(framesCompleted));
    }
    handleTaskEnd(failed->taskId,
        TaskRecord::Status::Failed,
        std::move(failed->error),
        framesCompleted);
    break;
  }
  case StudioMessageType::UIState: {
    // Opaque to every client: kept for the uiState.* asserts, never read
    // beyond the child names a script asks for.
    const auto state = decode<UIState>(msg);
    if (!state) {
      vsr::core::logError("[TestSession] undecodable UIState");
      event.fields.emplace_back("malformed", "true");
      break;
    }
    m_uiState = state->tree;
    event.fields.emplace_back("present", boolText(m_uiState != nullptr));
    event.fields.emplace_back("children",
        std::to_string(m_uiState ? m_uiState->root().numChildren() : 0));
    break;
  }
  default:
    vsr::core::logWarning(
        "[TestSession] %s is not handled by this client", toString(*type));
    break;
  }
  pushEvent(std::move(event));
}

void TestSession::handleTaskEnd(uint64_t taskId,
    TaskRecord::Status status,
    std::string message,
    uint64_t framesCompleted)
{
  auto &record = taskRecord(taskId);
  record.status = status;
  record.message = std::move(message);
  record.framesCompleted = framesCompleted;
  record.snapshotsAtEnd = m_snapshotsReceived;
  // A replayed end may repeat one heard live before the link dropped; both
  // are messages, and both count (README: assert values).
  ++(status == TaskRecord::Status::Completed ? m_tasksCompleted
                                             : m_tasksFailed);
  if (m_bootstrapping)
    ++m_tasksReplayed;
}

TaskRecord &TestSession::taskRecord(uint64_t taskId)
{
  if (!m_tasks.contains(taskId))
    m_tasks.set(taskId, TaskRecord{});
  return m_tasks[taskId];
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
  event.fields.emplace_back("buildInfo", quotedText(hello->buildInfo));
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
  if (m_lastFrameHeader && m_lastFrameHeader->frame != view->header.frame) {
    ++m_framesAdvanced;
    const int step = view->header.frame - m_lastFrameHeader->frame;
    if (step > m_frameMaxStep)
      m_frameMaxStep = step;
  }
  m_lastFrameHeader = view->header;
  m_lastFrame = std::move(*frame);
  ++m_framesReceived;
  pushEvent(frameEvent(view->header, view->size));
}

} // namespace vsr::scivis_studio::test_client
