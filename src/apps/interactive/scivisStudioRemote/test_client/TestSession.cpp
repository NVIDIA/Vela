// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TestSession.h"
#include "AnyText.h"
#include "CommandText.h"
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
// vsr_scene
#include "vsr/scene/Layer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <thread>

namespace vsr::scivis_studio::test_client {

using namespace protocol;
using vsr::network::Message;

namespace {

constexpr std::chrono::milliseconds POLL_INTERVAL{1};
// Between reconnect attempts a restarting server refuses.
constexpr std::chrono::milliseconds RECONNECT_PAUSE{100};
// What a BootstrapBegin says of the tasks the previous session left running.
constexpr const char *CONNECTION_LOST = "connection lost";

std::string endpointText(const std::string &host, uint16_t port)
{
  return host + ":" + std::to_string(port);
}

void recordHello(Event &event, const Message &msg)
{
  const auto hello = decode<Hello>(msg);
  if (!hello) {
    event.fields.emplace_back("malformed", "true");
    return;
  }
  event.fields.emplace_back("version", std::to_string(hello->version));
  event.fields.emplace_back("buildInfo", quotedText(hello->buildInfo));
}

// The connection's timings for a scripted session: the script's liveness
// numbers, and no automatic retry -- a loss is the script's to reconnect.
client::ConnectionTimings connectionTimings(SessionTimings timings)
{
  client::ConnectionTimings out;
  out.pingAfterQuiet = timings.pingAfterQuiet;
  out.lossAfterSilence = timings.lossAfterSilence;
  out.autoRetryFor = std::chrono::milliseconds{0};
  return out;
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

Event::Event(std::string n) : name(std::move(n)) {}

Event::Event(StudioMessageType t) : name(toString(t)), type(t) {}

const std::string *Event::field(const char *key) const
{
  for (const auto &[k, value] : fields) {
    if (k == key)
      return &value;
  }
  return nullptr;
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
  Event event(StudioMessageType::Frame);
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
    : m_connection(&m_mirror, connectionTimings(timings))
{
  m_connection.onMessage = [this](const Message &msg) { record(msg); };
}

TestSession::~TestSession() = default;

// Queries ////////////////////////////////////////////////////////////////////

SessionState TestSession::state() const
{
  return m_state;
}

const std::string &TestSession::host() const
{
  return m_connection.host();
}

uint16_t TestSession::port() const
{
  return m_connection.port();
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
  return m_connection.project();
}

const FrameConfig &TestSession::frameConfig() const
{
  return m_connection.frameConfig();
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

client::RequestHandle TestSession::sendPick(int x, int y, std::string *error)
{
  if (!requireConnected(error))
    return {};
  return m_connection.projectOps().pick(x, y, {});
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

std::optional<size_t> TestSession::snapshotsAtTaskEnd(uint64_t taskId) const
{
  const auto *record = m_tasks.at(taskId);
  if (!record || !record->finished())
    return {};
  return record->snapshotsAtEnd;
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

// Session ////////////////////////////////////////////////////////////////////

bool TestSession::connect(const std::string &host,
    uint16_t port,
    std::chrono::milliseconds deadline,
    std::string *error)
{
  // Connecting over an open link (or a half-open attempt) is an implicit
  // disconnect first; the state then says so.
  if (m_connection.phase() != client::SessionPhase::Idle)
    disconnect();
  m_failure.clear();
  m_connection.connect(host, port);

  // Every way an attempt ends lands in Idle, except success.
  pollUntil(
      [&] {
        return m_connection.bootstrapped()
            || m_connection.phase() == client::SessionPhase::Idle;
      },
      deadline);
  if (m_connection.bootstrapped())
    return true;
  m_failure = m_connection.lastFailure();
  if (m_connection.phase() != client::SessionPhase::Idle) {
    // The deadline: Connected was never entered (that takes BootstrapEnd), so
    // closing leaves the state as it was.
    m_failure = (m_connection.phase() == client::SessionPhase::AwaitingHello
                        ? "no Hello from "
                        : "no complete Bootstrap from ")
        + endpointText(host, port) + " within "
        + std::to_string(deadline.count()) + " ms";
    // The connection is left with no session; this session's state is the
    // one it had, since it was never Connected on this attempt.
    m_connection.disconnect();
  }
  if (error)
    *error = m_failure;
  return false;
}

bool TestSession::reconnect(
    std::chrono::milliseconds deadline, std::string *error)
{
  if (m_connection.host().empty()) {
    if (error)
      *error = "never connected: nothing to reconnect to";
    return false;
  }
  // The connection never retries a loss on its own here, and a server still
  // coming back refuses at once, so attempts repeat until the deadline, each
  // bounded by what is left of it.
  const auto end = Clock::now() + deadline;
  const std::string host = m_connection.host();
  const uint16_t port = m_connection.port();
  while (true) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - Clock::now());
    if (connect(host, port, std::max(left, {}), error))
      return true;
    if (Clock::now() + RECONNECT_PAUSE >= end)
      return false;
    std::this_thread::sleep_for(RECONNECT_PAUSE);
  }
}

void TestSession::disconnect()
{
  m_connection.disconnect();
  clearSessionRecords();
  if (m_state != SessionState::NeverConnected)
    setState(SessionState::Disconnected);
}

bool TestSession::shutdown(
    std::chrono::milliseconds deadline, std::string *error)
{
  if (!requireConnected(error))
    return false;
  m_connection.sendShutdown();

  const bool closed = pollUntil(
      [&] { return m_connection.phase() != client::SessionPhase::Closing; },
      deadline);
  // The connection drops the session on the close it asked for; a server that
  // kept the socket open is left here instead. Either way we meant to leave.
  disconnect();
  if (closed)
    return true;
  if (error) {
    *error = "server did not close the socket within "
        + std::to_string(deadline.count()) + " ms of Shutdown";
  }
  return false;
}

// Pumping ////////////////////////////////////////////////////////////////////

void TestSession::poll()
{
  // Every message the connection handles is recorded through onMessage as it
  // goes; frames never reach that hook.
  m_connection.poll();
  consumeFrame();
  syncState();
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
  if (m_connection.trySend(std::move(msg)))
    return true;
  // Ready and refused: the session went while this poll was in hand.
  if (error)
    *error = "the connection dropped the message";
  return false;
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
  if (!value.valid() || anari::isArray(value.type())) {
    // The delegate skips those, so the edit would stay local (arrays never
    // ride a SetObjectParameter).
    if (error)
      *error = "cannot set an array or empty value on a parameter";
    return false;
  }
  // The mirror's update delegate turns this into the SetObjectParameter.
  obj->addParameter(name).setValue(value);
  return true;
}

bool TestSession::removeParameter(
    const SceneObjectRef &object, const std::string &name, std::string *error)
{
  auto *obj = mirrorObject(object, error);
  if (!obj)
    return false;
  // Likewise the RemoveObjectParameter.
  obj->removeParameter(name);
  return true;
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
  // The delegate cannot build this one: a layer signal does not name the
  // node that moved (MirrorUpdateDelegate.h).
  SetNodeTransform edit;
  edit.node = node;
  edit.transform = transform;
  return send(edit, error);
}

// State //////////////////////////////////////////////////////////////////////

void TestSession::setState(SessionState to)
{
  if (to == m_state)
    return;
  vsr::core::logStatus(
      "[TestSession] %s -> %s", toString(m_state), toString(to));
  m_state = to;
}

void TestSession::syncState()
{
  if (m_connection.bootstrapped()) {
    // Connected means fully populated: only with BootstrapEnd are mirror and
    // replica the server's.
    setState(SessionState::Connected);
    return;
  }
  if (m_state != SessionState::Connected
      || m_connection.phase() != client::SessionPhase::Idle) {
    // Before the first BootstrapEnd every ending is one more failed attempt,
    // and the state stays as it was.
    return;
  }
  // An established link ended: involuntarily unless the connection says the
  // session was dropped on purpose (a Shutdown's close, a mismatched server
  // met while Lost). Lost keeps replies, picks and task records as the frozen
  // view they belong to; a dropped session takes them with it.
  m_failure = m_connection.lastFailure();
  if (m_connection.state() == client::ConnectionState::Disconnected) {
    clearSessionRecords();
    setState(SessionState::Disconnected);
  } else {
    setState(SessionState::Lost);
  }
}

void TestSession::clearSessionRecords()
{
  m_replies.clear();
  m_pickReplies.clear();
  m_tasks.clear();
}

bool TestSession::requireConnected(std::string *error) const
{
  // Connected and still Ready: the phase the connection takes a send in, and
  // the one a loss this poll has not yet been noticed in has already left.
  if (m_state == SessionState::Connected
      && m_connection.phase() == client::SessionPhase::Ready)
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
    *error = "no " + objectRefText(object) + " in the mirror";
  return obj;
}

// Recording //////////////////////////////////////////////////////////////////

void TestSession::pushEvent(Event event)
{
  m_events.push_back(std::move(event));
}

void TestSession::record(const Message &msg)
{
  const auto type = messageType(msg);
  if (!type) {
    Event event("Unknown");
    event.fields.emplace_back("type", std::to_string(int(msg.header.type)));
    pushEvent(std::move(event));
    return;
  }

  Event event(*type);
  // What the connection made of the message is already done: it is Ready
  // exactly while a session is established, and inside the bracket while a
  // Bootstrap replays.
  const bool bootstrapped = m_connection.bootstrapped();
  const bool bootstrapping = m_connection.bootstrapping();

  if (isSceneMessageType(*type)) {
    recordSceneMessage(event);
    pushEvent(std::move(event));
    return;
  }

  switch (*type) {
  case StudioMessageType::Hello:
    recordHello(event, msg);
    break;
  case StudioMessageType::Pong:
    break;
  case StudioMessageType::Error: {
    const auto error = decode<Error>(msg);
    const std::string text = error ? error->message : "(undecodable Error)";
    event.fields.emplace_back("message", quotedText(text));
    // An Error before the first BootstrapEnd is the server turning the
    // attempt down, not a session error: the connection has already failed
    // the attempt over it.
    if (bootstrapped) {
      m_lastError = text;
      ++m_errorsReceived;
    }
    break;
  }
  case StudioMessageType::Disconnect:
    // The server's farewell; the connection keeps its reason as the one the
    // close that follows is explained by.
    event.fields.emplace_back(
        "reason", quotedText(farewellReason(decode<Disconnect>(msg))));
    break;
  case StudioMessageType::BootstrapBegin:
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
    break;
  case StudioMessageType::FrameConfig: {
    const auto config = decode<FrameConfig>(msg);
    if (!config) {
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.fields.emplace_back("width", std::to_string(config->width));
    event.fields.emplace_back("height", std::to_string(config->height));
    break;
  }
  case StudioMessageType::ProjectSnapshot: {
    // Decoded again rather than read off the replica: an undecodable
    // snapshot leaves the replica as it was, and the record must say so.
    const auto snapshot = decode<ProjectSnapshot>(msg);
    if (!snapshot) {
      event.fields.emplace_back("malformed", "true");
      break;
    }
    const auto *project = &snapshot->project;
    ++m_snapshotsReceived;
    event.fields.emplace_back("activeShot", project->activeShotId);
    event.fields.emplace_back("shots", std::to_string(project->shots.size()));
    event.fields.emplace_back(
        "datasets", std::to_string(project->datasets.size()));
    event.fields.emplace_back(
        "lightRigs", std::to_string(project->lightRigs.size()));
    event.fields.emplace_back(
        "cameraRigs", std::to_string(project->cameraRigs.size()));
    event.fields.emplace_back(
        "colorMaps", std::to_string(project->colorMaps.size()));
    event.fields.emplace_back("dirty", boolText(project->dirty));
    // Time at Rest, as the snapshot carries it for the active shot.
    if (const auto *shot = project::activeShot(*project)) {
      event.fields.emplace_back("playing", boolText(shot->playing));
      event.fields.emplace_back(
          "currentFrame", std::to_string(shot->currentFrame));
    }
    break;
  }
  case StudioMessageType::ProjectOpReply: {
    auto reply = decode<ProjectOpReply>(msg);
    if (!reply) {
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.requestId = reply->requestId;
    event.fields.emplace_back("requestId", std::to_string(reply->requestId));
    event.fields.emplace_back("ok", boolText(reply->ok));
    event.fields.emplace_back("error", quotedText(reply->error));
    if (!reply->ok) {
      ++m_repliesFailed;
      m_lastReplyError = reply->error;
    } else if (const auto started = results<TaskStartedResult>(*reply)) {
      // Queued from the launch, so a loss before its first progress still
      // leaves a record to fail. A record already under the id belongs to a
      // task of a server process since restarted (ids count from 1 again):
      // the new task takes it over.
      m_tasks[started->taskId] = TaskRecord{};
    }
    m_replies[reply->requestId] = {std::move(*reply), m_snapshotsReceived};
    break;
  }
  case StudioMessageType::PickReply: {
    auto reply = decode<PickReply>(msg);
    if (!reply) {
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.requestId = reply->requestId;
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
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.taskId = progress->taskId;
    event.fields.emplace_back("taskId", std::to_string(progress->taskId));
    event.fields.emplace_back("current", std::to_string(progress->current));
    event.fields.emplace_back("total", std::to_string(progress->total));
    event.fields.emplace_back("message", quotedText(progress->message));
    // A task ends once and the replay repeats endings, not progress, so
    // progress for a finished record is a new task: a restarted server
    // reusing the id, or the replay reviving one this client failed at
    // BootstrapBegin. Either way the record starts over.
    auto &record = taskRecord(progress->taskId);
    if (record.finished())
      record = TaskRecord{};
    record.status = TaskRecord::Status::Running;
    ++record.progressReports;
    record.current = progress->current;
    record.total = progress->total;
    if (bootstrapping)
      ++m_tasksReplayed;
    break;
  }
  case StudioMessageType::TaskCompleted: {
    auto completed = decode<TaskCompleted>(msg);
    if (!completed) {
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.taskId = completed->taskId;
    event.fields.emplace_back("taskId", std::to_string(completed->taskId));
    event.fields.emplace_back("message", quotedText(completed->message));
    const auto frames = framesCompletedOf(*completed);
    if (frames)
      event.fields.emplace_back("framesCompleted", std::to_string(frames));
    handleTaskEnd(completed->taskId,
        TaskRecord::Status::Completed,
        std::move(completed->message),
        frames);
    break;
  }
  case StudioMessageType::TaskFailed: {
    auto failed = decode<TaskFailed>(msg);
    if (!failed) {
      event.fields.emplace_back("malformed", "true");
      break;
    }
    event.taskId = failed->taskId;
    event.fields.emplace_back("taskId", std::to_string(failed->taskId));
    event.fields.emplace_back("error", quotedText(failed->error));
    const auto frames = framesCompletedOf(*failed);
    if (frames)
      event.fields.emplace_back("framesCompleted", std::to_string(frames));
    handleTaskEnd(failed->taskId,
        TaskRecord::Status::Failed,
        std::move(failed->error),
        frames);
    break;
  }
  default:
    break;
  }
  pushEvent(std::move(event));
}

void TestSession::recordSceneMessage(Event &event)
{
  // The connection answers a refused push with an Error and counts it; the
  // stamp is re-derived here so a refusal stays visible to a script.
  const auto refusals = m_connection.sceneRefusals();
  if (refusals != m_sceneRefusals) {
    m_sceneRefusals = refusals;
    event.fields.emplace_back("malformed", "true");
  }
  event.fields.emplace_back("objects", std::to_string(totalObjects(m_mirror)));
  event.fields.emplace_back(
      "layers", std::to_string(m_mirror.numberOfLayers()));
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
  if (status == TaskRecord::Status::Completed)
    ++m_tasksCompleted;
  else
    ++m_tasksFailed;
  if (m_connection.bootstrapping())
    ++m_tasksReplayed;
}

TaskRecord &TestSession::taskRecord(uint64_t taskId)
{
  if (!m_tasks.contains(taskId))
    m_tasks.set(taskId, TaskRecord{});
  return m_tasks[taskId];
}

void TestSession::consumeFrame()
{
  Message frame;
  if (!m_connection.takeLatestFrame(frame))
    return;

  Event event(StudioMessageType::Frame);
  const auto view = decodeFrame(frame);
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
  m_lastFrame = std::move(frame);
  ++m_framesReceived;
  pushEvent(frameEvent(view->header, view->size));
}

} // namespace vsr::scivis_studio::test_client
