// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client_core
#include "ProjectOps.h"
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PlaybackMessages.h"
#include "ProjectOpReply.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/Message.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/FlatMap.hpp"
#include "vsr/core/TypeMacros.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::scivis_studio {
struct Project;
}

namespace vsr::scivis_studio::test_client {

// The client's Connection State (CONTEXT.md). Connected is entered with
// BootstrapEnd, when mirror and replica are the server's. Lost is involuntary:
// the Structural Mirror and Project Replica stay as a frozen view until a
// reconnect() or disconnect(). Disconnected is a completed intention: mirror
// and replica are cleared.
enum class SessionState
{
  NeverConnected,
  Connected,
  Lost,
  Disconnected
};

const char *toString(SessionState state);

// Liveness timings: a Ping after this much quiet, loss after this much
// silence. The spec's defaults; the liveness test shrinks them.
struct SessionTimings
{
  std::chrono::milliseconds pingAfterQuiet{5000};
  std::chrono::milliseconds lossAfterSilence{15000};
};

// One record of the event stream: a server message as the session consumed
// it, or an entry the runner adds under a reply (a DirectoryEntry). Its
// identity is typed -- the message type, and the request or task id a reply
// or task message carries -- so a wait matches on those; the name and the
// key/value fields are its text (`Frame width=64 height=48 ...`).
struct Event
{
  Event() = default;
  // A record of the runner's own (DatasetCandidate, DataRoot, ...), or of a
  // type byte outside the Studio set.
  explicit Event(std::string name);
  // A server message's record, named after its type.
  explicit Event(protocol::StudioMessageType type);

  std::string name;
  // The message this records, when it is one.
  std::optional<protocol::StudioMessageType> type;
  // The request id of a ProjectOpReply or PickReply.
  std::optional<uint64_t> requestId;
  // The task id of a TaskProgress, TaskCompleted or TaskFailed.
  std::optional<uint64_t> taskId;
  std::vector<std::pair<std::string, std::string>> fields;

  // The value of that field as the record prints it; null when it has none.
  const std::string *field(const char *key) const;
  std::string text() const;
};

// The Frame record, as both the event stream and dump-frame print it.
Event frameEvent(const protocol::FrameHeader &header, size_t bytes);

// A Server Task as the session has heard of it: Queued from the reply that
// launched it (a TaskStartedResult), Running from its first TaskProgress,
// until the one TaskCompleted (message) or TaskFailed (error). A task whose
// end arrives before anything else of it -- another client's, or one a
// Bootstrap replays -- is known only once it has ended. A BootstrapBegin
// fails every unfinished record with "connection lost": the replay that
// follows rebuilds the truth.
struct TaskRecord
{
  enum class Status
  {
    Queued,
    Running,
    Completed,
    Failed
  };
  Status status{Status::Queued};
  std::string message; // the completion message, or the failure's error
  // TaskProgress messages heard, and the newest one's current of total
  // (0 = indeterminate).
  size_t progressReports{0};
  uint64_t current{0};
  uint64_t total{0};
  // Frames written, from the ending's RenderShotResult (a RenderShot; 0 else).
  uint64_t framesCompleted{0};
  // Snapshots applied when the end message arrived; the server sends a
  // task's snapshot after its TaskCompleted, so one past this count is it.
  size_t snapshotsAtEnd{0};

  bool finished() const;
};

const char *toString(TaskRecord::Status status);

// Visits every object pool of a Scene as (ANARI type, pool), in the order
// dump-scene lists them. Arrays ride the Structural Mirror as descriptors, so
// they count too.
template <typename Visitor>
void forEachObjectPool(const vsr::scene::ObjectDatabase &db, Visitor &&visit)
{
  visit(ANARI_ARRAY, db.array);
  visit(ANARI_SURFACE, db.surface);
  visit(ANARI_GEOMETRY, db.geometry);
  visit(ANARI_MATERIAL, db.material);
  visit(ANARI_SAMPLER, db.sampler);
  visit(ANARI_VOLUME, db.volume);
  visit(ANARI_SPATIAL_FIELD, db.field);
  visit(ANARI_LIGHT, db.light);
  visit(ANARI_CAMERA, db.camera);
  visit(ANARI_RENDERER, db.renderer);
}

// Objects of every type in `scene`: what the `scene.objects` assert value and
// the scene-transfer events report.
size_t totalObjects(const vsr::scene::Scene &scene);

/*
 * The test client's session with a Studio server: the GUI client's
 * ServerConnection, driven headlessly and recorded. The connection owns the
 * protocol -- TCP connect, the Hello exchange, the bracketed Bootstrap into
 * the Structural Mirror and Project Replica this session owns, Ping/Pong
 * liveness, loss detection -- and this struct adds what a script needs and a
 * UI does not: an event stream, counters, and the replies, picks and Server
 * Task records kept by id, so a wait can be on state rather than on the
 * order messages happen to arrive in.
 *
 * It records from ServerConnection::onMessage, which fires for every message
 * poll() consumed once it has been handled, so the mirror, replica and phase
 * an event reports are the ones the message left behind. Frames never reach
 * that hook (they are a latest-wins slot); poll() takes the newest one and
 * records it. Time in motion is read off the Frame headers: the session
 * counts the frames whose `frame` differed from the previous one, and the
 * largest forward step between two consecutive headers.
 *
 * Project Ops go out through sendRequest(), which is ProjectOps::send() with
 * no callback: the reply is picked up from the event stream by request id,
 * as picks are. Scene edits are optimistic in the mirror, and the
 * connection's MirrorUpdateDelegate turns the parameter ones into their
 * messages.
 *
 * Loss never retries on its own here (autoRetryFor is 0): reconnect() is the
 * script's word. Every wait takes a deadline and returns false with the
 * reason when it passes; nothing blocks unboundedly. Every public member runs
 * on the caller's thread.
 *
 * Example:
 *   TestSession session;
 *   std::string error;
 *   if (!session.connect("127.0.0.1", 12345, 5s, &error))
 *     return fail(error);
 *   session.startRendering();
 *   session.pollUntil([&] { return session.framesReceived() > 0; }, 5s);
 *   Event event;
 *   while (session.takeEvent(event))
 *     std::cout << "EVT " << event.text() << '\n';
 */
struct TestSession
{
  explicit TestSession(SessionTimings timings = {});
  ~TestSession();

  VSR_NOT_COPYABLE(TestSession)
  VSR_NOT_MOVEABLE(TestSession)

  // Queries (valid between polls) //

  SessionState state() const;
  const std::string &host() const;
  uint16_t port() const;
  // The Structural Mirror; edits made through setParameter() and friends
  // land here as well as on the wire.
  vsr::scene::Scene &mirror();
  const vsr::scene::Scene &mirror() const;
  // The Project Replica; null before the first snapshot and once
  // Disconnected.
  const Project *project() const;
  const protocol::FrameConfig &frameConfig() const;
  // The newest Frame poll() consumed; empty until the first one.
  const std::optional<protocol::FrameHeader> &lastFrameHeader() const;
  const vsr::network::Message &lastFrame() const;
  // Frames poll() consumed (frames the slot dropped are not counted).
  size_t framesReceived() const;
  // Consumed frames whose header `frame` differed from the previous one's
  // (the first frame counts as none), and the largest forward step between
  // two consecutive headers. A step backwards (a loop wrap to 0, a scrub) is
  // time moving on purpose, not a skip, so it does not count as a step.
  size_t framesAdvanced() const;
  int frameMaxStep() const;
  size_t errorsReceived() const;
  const std::string &lastError() const;
  // Why the last connect attempt failed or the link was Lost.
  const std::string &failure() const;

  // Project Ops and Server Tasks (valid between polls) //

  // Mints a request id, sends the request through the connection's
  // ProjectOps and answers with its handle; an invalid one with the reason
  // when the session is not Connected. The reply is not awaited: it lands in
  // the event stream and under reply(handle.requestId).
  template <typename Req>
  client::RequestHandle sendRequest(Req request, std::string *error = nullptr);
  // Likewise for a Pick at that pixel (x right, y down from the top-left of
  // the frame); its answer is a PickReply, kept under pickReply().
  client::RequestHandle sendPick(int x, int y, std::string *error = nullptr);
  // The reply the server sent to that request id; null until it arrives, and
  // once the session is Disconnected.
  const protocol::ProjectOpReply *reply(uint64_t requestId) const;
  // How many snapshots had been applied when that reply was; the server
  // sends an op's snapshot after its reply, so a snapshot past this count is
  // one the request (or a later one) caused. Empty until the reply arrives.
  std::optional<size_t> snapshotsAtReply(uint64_t requestId) const;
  // Likewise for a task's end message, which its snapshot follows. Empty
  // until the task has ended.
  std::optional<size_t> snapshotsAtTaskEnd(uint64_t taskId) const;
  // Replies with ok == false, counted over the session's lifetime.
  size_t repliesFailed() const;
  // What the session has heard of a task; null before its first message and
  // once the session is Disconnected.
  const TaskRecord *task(uint64_t taskId) const;
  // TaskCompleted / TaskFailed messages received over the session's lifetime,
  // reconnects and replays included; the "connection lost" failures a
  // BootstrapBegin declares are not messages and do not count.
  size_t tasksCompleted() const;
  size_t tasksFailed() const;
  // Task messages (progress or end) the newest Bootstrap carried between its
  // Begin and End: the server's task-status replay.
  size_t tasksReplayed() const;
  // Project Snapshots applied so far, the Bootstrap's included.
  size_t snapshotsReceived() const;
  // The error text of the newest reply with ok == false; empty until one.
  const std::string &lastReplyError() const;
  // The PickReply to that request id; null until it arrives, and once the
  // session is Disconnected.
  const protocol::PickReply *pickReply(uint64_t requestId) const;
  // TimeAdvanceWarnings received, and the newest one (empty until the first).
  size_t warningsReceived() const;
  const std::optional<protocol::TimeAdvanceWarning> &lastWarning() const;
  // The newest UIState tree the server sent (a Bootstrap's, or the one that
  // follows an OpenProject); null until one, when it was null, and once the
  // session is Disconnected. Opaque here as everywhere: asserts read
  // `windows/<key>` leaves, nothing interprets them.
  const protocol::SubtreePtr &uiState() const;

  // Session //

  // Connects, exchanges Hellos and waits for the complete Bootstrap. False
  // with the reason on refusal, version mismatch, socket loss or the deadline;
  // the state is then left as it was, and another connect() may follow at
  // once. Called on an open link it disconnect()s first.
  bool connect(const std::string &host,
      uint16_t port,
      std::chrono::milliseconds deadline,
      std::string *error = nullptr);
  // connect() again to the last host and port, retrying refused attempts
  // until the deadline: a fresh handshake and Bootstrap that wholesale-replace
  // mirror and replica.
  bool reconnect(
      std::chrono::milliseconds deadline, std::string *error = nullptr);
  // Sends Disconnect, closes, clears mirror and replica -> Disconnected.
  void disconnect();
  // Sends Shutdown and waits for the server to close the socket ->
  // Disconnected. False when the socket is still open at the deadline (it is
  // then closed locally).
  bool shutdown(
      std::chrono::milliseconds deadline, std::string *error = nullptr);

  // Pumping //

  // Drains inbound messages into the mirror, replica and event queue, runs
  // the ping/loss timers and notices a lost link.
  void poll();
  // Pops the oldest unconsumed event; false when there is none.
  bool takeEvent(Event &out);
  // Polls until `done` holds or the deadline passes; false on timeout.
  bool pollUntil(
      const std::function<bool()> &done, std::chrono::milliseconds deadline);

  // Outbound (false with the reason unless Connected) //

  bool send(vsr::network::Message &&msg, std::string *error = nullptr);
  template <typename T>
  bool send(const T &payload, std::string *error = nullptr);
  // A message of an arbitrary type byte with verbatim payload bytes, for
  // probing the server's rejection paths.
  bool sendRaw(uint8_t type,
      std::vector<std::byte> payload,
      std::string *error = nullptr);
  bool ping(std::string *error = nullptr);
  bool setFrameConfig(
      uint32_t width, uint32_t height, std::string *error = nullptr);
  bool setEncodings(const std::vector<protocol::FrameEncoding> &preferred,
      std::string *error = nullptr);
  bool startRendering(std::string *error = nullptr);
  bool stopRendering(std::string *error = nullptr);
  // Optimistic scene edits: applied to the mirror and sent (the parameter
  // ones by the mirror's own update delegate). False when the mirror has no
  // such object or layer. The node index of a transform is the server's; the
  // mirror is updated only when it has a transform node there.
  bool setParameter(const SceneObjectRef &object,
      const std::string &name,
      const vsr::core::Any &value,
      std::string *error = nullptr);
  bool removeParameter(const SceneObjectRef &object,
      const std::string &name,
      std::string *error = nullptr);
  bool setNodeTransform(const SceneNodeRef &node,
      const vsr::math::mat4 &transform,
      std::string *error = nullptr);

 private:
  using Clock = std::chrono::steady_clock;

  void setState(SessionState to);
  // What the connection did since the last look: Connected once it is
  // bootstrapped, Lost or Disconnected once an established link ended.
  void syncState();
  // What the connection dropped when it dropped the session.
  void clearSessionRecords();
  // Records one handled message as an Event and in the counters and maps.
  void record(const vsr::network::Message &msg);
  // The object and layer counts the mirror holds now, and the malformed stamp
  // when this message is the one the mirror refused.
  void recordSceneMessage(Event &event);
  // The newest Frame the connection took, if any.
  void consumeFrame();
  void handleTaskEnd(uint64_t taskId,
      TaskRecord::Status status,
      std::string message,
      uint64_t framesCompleted);
  // The record of a task, made if the task is new.
  TaskRecord &taskRecord(uint64_t taskId);
  void pushEvent(Event event);
  bool requireConnected(std::string *error) const;
  // The mirror's object for an edit, or null with the reason.
  vsr::scene::Object *mirrorObject(
      const SceneObjectRef &object, std::string *error);

  // Declared before the connection, which installs its update delegate on
  // the mirror and must let go of it first.
  vsr::scene::Scene m_mirror;
  client::ServerConnection m_connection;

  SessionState m_state{SessionState::NeverConnected};
  std::string m_failure; // why the last attempt failed or the link was lost

  std::optional<protocol::FrameHeader> m_lastFrameHeader;
  vsr::network::Message m_lastFrame;
  size_t m_framesReceived{0};
  size_t m_framesAdvanced{0};
  int m_frameMaxStep{0};
  size_t m_errorsReceived{0};
  std::string m_lastError;
  struct ReceivedReply
  {
    protocol::ProjectOpReply reply;
    size_t snapshotsReceived{0}; // when it arrived
  };
  vsr::core::FlatMap<uint64_t, ReceivedReply> m_replies;
  size_t m_repliesFailed{0};
  std::string m_lastReplyError;
  vsr::core::FlatMap<uint64_t, protocol::PickReply> m_pickReplies;
  size_t m_warningsReceived{0};
  std::optional<protocol::TimeAdvanceWarning> m_lastWarning;
  vsr::core::FlatMap<uint64_t, TaskRecord> m_tasks;
  size_t m_tasksCompleted{0};
  size_t m_tasksFailed{0};
  size_t m_tasksReplayed{0};
  size_t m_snapshotsReceived{0};
  // The connection's refusal count as of the last scene message recorded.
  uint64_t m_sceneRefusals{0};
  std::deque<Event> m_events;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool TaskRecord::finished() const
{
  return status == Status::Completed || status == Status::Failed;
}

template <typename T>
inline bool TestSession::send(const T &payload, std::string *error)
{
  return send(protocol::encode(payload), error);
}

template <typename Req>
inline client::RequestHandle TestSession::sendRequest(
    Req request, std::string *error)
{
  if (!requireConnected(error))
    return {};
  return m_connection.projectOps().send(std::move(request), {});
}

} // namespace vsr::scivis_studio::test_client
