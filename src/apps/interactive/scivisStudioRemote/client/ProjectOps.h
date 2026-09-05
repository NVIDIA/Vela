// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "PlaybackMessages.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "ShotRigRequests.h"
#include "StudioCodec.h"
#include "TaskMessages.h"
#include "ViewportMessages.h"
// vsr_network
#include "vsr/network/Message.hpp"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vsr::scivis_studio::client {

// Runs on the UI thread from ServerConnection::poll() with the server's
// answer; a failed op has `ok == false` and a user-facing `error`.
using ReplyCallback = std::function<void(const protocol::ProjectOpReply &)>;

// ReplyCallback plus the decoded *Result payload; empty when the reply failed
// or carried nothing decodable as an R.
template <typename R>
using ResultCallback = std::function<void(
    const protocol::ProjectOpReply &, const std::optional<R> &)>;

// The server's PickReply for one Pick, matched by request id; absent when the
// reply can no longer come (the connection dropped the request or was lost).
using PickCallback =
    std::function<void(const std::optional<protocol::PickReply> &)>;

// Names one request in flight; valid() only for a request that was minted.
struct RequestHandle
{
  uint64_t requestId{0};

  bool valid() const;
};

// Where a Server Task stands as far as this client has heard.
enum class TaskState
{
  Queued,
  Running,
  Completed,
  Failed
};

const char *toString(TaskState state);

struct TaskProgressInfo
{
  uint64_t current{0};
  uint64_t total{0}; // 0 = indeterminate
  std::string message;
};

// One Server Task this client launched or heard about; finished records stay
// until ProjectOps::clearFinishedTasks().
struct TaskRecord
{
  uint64_t taskId{0};
  std::string label; // from the launching request, for the task panel
  TaskState state{TaskState::Queued};
  TaskProgressInfo lastProgress; // keeps the last phase text once finished
  std::string outcome; // TaskCompleted::message (an output directory, a name)
  std::string error; // TaskFailed::error
  // Frames a render wrote, from the ending's RenderShotResult (a cancelled
  // or failed render leaves its partial frames on disk); 0 for other tasks.
  uint64_t framesCompleted{0};
  // Launched by this client's RenderShot: the editors show a note while it
  // is active, since the server refuses edits until the render ends.
  bool render{false};
  // Failed by this client at BootstrapBegin ("connection lost"), not by the
  // server, which may still be running the task: the replay's word on it is
  // news (its ending fires onTaskEnded, its progress starts the record over
  // keeping `render`). Cleared as soon as the server speaks of the task.
  bool failedByClient{false};

  bool finished() const;
  // The one-line toast for a finished record: "<label> completed (N frames):
  // <outcome>" or "<label> failed after N frames: <error>", each part only
  // when the record has it ("<task>" stands in for a missing label). Empty
  // unless finished().
  std::string describeEnding() const;
};

// The TaskRecord label of a request that starts a Server Task, from its
// fields ("Open project '/d/p'", "Render shot 'shot_0001'"); empty for a
// request that starts none. send() names the record with it.
std::string taskLabel(const protocol::OpenProject &req);
std::string taskLabel(const protocol::SaveProject &req);
std::string taskLabel(const protocol::ImportStaticDataset &req);
std::string taskLabel(const protocol::ImportSubtreeDataset &req);
std::string taskLabel(const protocol::ImportFileAnimationDataset &req);
std::string taskLabel(const protocol::ReimportDataset &req);
std::string taskLabel(const protocol::LoadDataset &req);
std::string taskLabel(const protocol::SaveDatasetArchive &req);
std::string taskLabel(const protocol::LoadDatasetArchive &req);
std::string taskLabel(const protocol::IncorporateDatasetCandidate &req);
std::string taskLabel(const protocol::RenderShot &req);
template <typename Req>
std::string taskLabel(const Req &);

/*
 * The client's side of every Project Op: mints request ids, sends the
 * request, and hands the matching ProjectOpReply to the caller's callback;
 * tracks the Server Tasks those replies start; and offers Remote Browse. The
 * client applies nothing optimistically -- the reply and the following
 * Project Snapshot are the truth, so the callbacks carry only the reply.
 *
 * Owned by ServerConnection, which feeds it the inbound replies and task
 * events and fails every pending callback exactly once with "connection
 * lost" when it declares loss or disconnects (the spec's "connection-scoped
 * request failure"). Everything here runs on the UI thread; callbacks fire
 * only from ServerConnection::poll(), never from inside send().
 *
 * A caller fills in the protocol's request struct (requestId excepted) and
 * hands it to send(), or to sendForResult<R>() when the reply carries a
 * *Result payload to decode. A request that starts a Server Task (its reply
 * is a TaskStartedResult) registers the task in tasks() under taskLabel(req).
 * A Pick shares the id space but is answered by a plain PickReply, so it has
 * its own callback type but shares the pending list, so pending(), forget()
 * and the connection-loss failure cover it all the same; a server Error
 * naming a Pick ("Pick 7 refused: ...") fails the oldest pending one.
 *
 * Example:
 *   auto &ops = connection.projectOps();
 *   CreateShot create;
 *   create.name = "Shot 2";
 *   ops.sendForResult<ShotCreatedResult>(create,
 *       [&](const ProjectOpReply &reply,
 *           const std::optional<ShotCreatedResult> &r) {
 *         if (reply.ok) selectShot(r->shotId);
 *         else showError(reply.error);
 *       });
 *   OpenProject open;
 *   open.directory = "/data/run7";
 *   ops.sendForResult<TaskStartedResult>(open, [](auto &, auto &) {});
 *   for (const TaskRecord &task : ops.tasks())
 *     drawTaskRow(task);
 */
struct ProjectOps
{
  // Hands an encoded request to the connection; false when the connection
  // dropped it (not Connected), which fails the request on the next poll().
  using Sender = std::function<bool(vsr::network::Message &&)>;

  explicit ProjectOps(Sender sender);

  VSR_NOT_COPYABLE(ProjectOps)
  VSR_NOT_MOVEABLE(ProjectOps)

  // Generic sends ////////////////////////////////////////////////////////////

  // Mints req.requestId, sends, and stores the callback under that id;
  // taskLabel(req) names the TaskRecord if the reply starts a task.
  template <typename Req>
  RequestHandle send(Req req, ReplyCallback callback);
  // send() decoding the reply's results as an R for the callback.
  template <typename R, typename Req>
  RequestHandle sendForResult(Req req, ResultCallback<R> callback);
  // x right, y down from the top-left of the frame, in frame-header pixels.
  RequestHandle pick(int x, int y, PickCallback callback);

  // Project ops and picks awaiting their reply.
  size_t pendingCount() const;
  bool pending(RequestHandle handle) const;
  // Drops the callback; the reply, if it comes, is still used for task
  // tracking and otherwise ignored. A pick is retired outright: the server
  // answers only the surviving Pick, so nothing would ever retire it.
  void forget(RequestHandle handle);

  // Server Tasks (61) ////////////////////////////////////////////////////////

  // Runs with the record a TaskCompleted/TaskFailed just finished, from
  // handleTaskCompleted/Failed (so from ServerConnection::poll()), once per
  // ending that is news: a replayed ending for a record already finished by
  // the server's word is not (the bootstrap replays every ending it has not
  // replayed before, whether or not this client saw it live), and the
  // client's own failures at BootstrapBegin are not endings (the banner says
  // it) -- the replay's ending for such a record is.
  std::function<void(const TaskRecord &)> onTaskEnded;

  // In the order the tasks were first heard of.
  const std::vector<TaskRecord> &tasks() const;
  const TaskRecord *task(uint64_t taskId) const;
  // Any task Queued or Running.
  bool tasksActive() const;
  // A render this client launched is Queued or Running.
  bool renderActive() const;
  void clearFinishedTasks();

  // Driven by ServerConnection ///////////////////////////////////////////////

  // Matches the reply to its callback and registers a started task.
  void handleReply(const protocol::ProjectOpReply &reply);
  // A bare Error from the server that names a request type ("malformed
  // CreateShot payload", "Pick 7 refused: ...") retires the oldest pending
  // request of that type with `message` as its error (a pick with an absent
  // reply); true when one was. Servers answer such requests with a
  // ProjectOpReply when the payload carried an id; this covers the ones that
  // cannot, and picks, which have no reply to carry an error.
  bool failOldestNamed(const std::string &message);
  // Matches a PickReply to its pick() callback; unknown ids are logged.
  void handlePickReply(const protocol::PickReply &reply);
  // Task events create a record when the task is unknown (a task-status
  // replay during bootstrap, or one another client launched); a record
  // created by a TaskProgress takes its message as label, which is how the
  // bootstrap replay names the running task.
  void handleTaskProgress(const protocol::TaskProgress &progress);
  void handleTaskCompleted(const protocol::TaskCompleted &completed);
  void handleTaskFailed(const protocol::TaskFailed &failed);
  // Every pending callback runs once with an error reply carrying `error`
  // (picks with an absent reply), and the pending list is emptied first so a
  // callback may send anew.
  void failAllPending(const std::string &error);
  // BootstrapBegin: every Queued or Running record becomes Failed with
  // `error`, marked `failedByClient` and without an onTaskEnded; the
  // bootstrap's task-status replay then overwrites the ones the server still
  // knows about like any other event.
  void failUnfinishedTasks(const std::string &error);
  void clearTasks();
  // Delivers the failures of sends the connection dropped.
  void poll();

 private:
  // A project op's or a pick's callback; a pick is answered by a PickReply
  // and fails with an absent one.
  using Callback = std::variant<ReplyCallback, PickCallback>;

  struct Pending
  {
    uint64_t requestId{0};
    protocol::StudioMessageType type{};
    Callback callback;
    std::string taskLabel;

    bool hasCallback() const;
    bool isPick() const;
  };

  RequestHandle submit(uint64_t requestId,
      protocol::StudioMessageType type,
      vsr::network::Message &&msg,
      Callback callback,
      std::string taskLabel);
  // Takes the entry out of the pending list, if it is there.
  std::optional<Pending> takePending(uint64_t requestId);
  // Runs the entry's callback with a failed reply: the reply itself for a
  // project op, an absent PickReply for a pick.
  static void fail(Pending &entry, const protocol::ProjectOpReply &reply);
  TaskRecord *findRecord(uint64_t taskId);
  // The record of `taskId`, made if new.
  TaskRecord &recordFor(uint64_t taskId);
  // The record of `taskId` as if newly heard of (Queued "Task N", nothing
  // else kept): a new task under the id. The one exception is `render` on a
  // record this client failed at BootstrapBegin (failedByClient): the server
  // never ended that task, so a render it named may still be running and
  // still refusing edits.
  TaskRecord &startOver(uint64_t taskId);
  // Whether a TaskCompleted/TaskFailed for `record` would be news, and the
  // onTaskEnded call for one that is.
  static bool endingIsNews(const TaskRecord &record);
  void announceEnding(const TaskRecord &record);
  Pending *findPending(uint64_t requestId);
  const Pending *findPending(uint64_t requestId) const;

  Sender m_sender;
  uint64_t m_nextRequestId{1};
  // A handful of requests and picks at most; insertion order is the send
  // order.
  std::vector<Pending> m_pending;
  // Error replies for requests the connection would not send, delivered on
  // the next poll() so callbacks never run from inside send().
  std::vector<protocol::ProjectOpReply> m_undeliverable;
  std::vector<TaskRecord> m_tasks;
};

/*
 * One request at a time for a group of controls: remembers the last send,
 * is busy() while its reply is outstanding, and refuses to send while busy,
 * so "one op per group" is stated here once rather than as a
 * BeginDisabled(pending(handle)) and a handle assignment at every control.
 *
 * Example:
 *   ImGui::BeginDisabled(m_rigOp.busy(ops));
 *   if (ImGui::Button("Remove Rig")) {
 *     RemoveLightRig remove;
 *     remove.lightRigId = rig.id;
 *     m_rigOp.send(ops, std::move(remove), errorReporter);
 *   }
 *   ImGui::EndDisabled();
 */
struct InFlight
{
  // The last request sent; valid() once one was.
  RequestHandle handle;

  bool busy(const ProjectOps &ops) const;
  // ProjectOps::send()/sendForResult() unless busy; false when refused.
  template <typename Req>
  bool send(ProjectOps &ops, Req req, ReplyCallback callback);
  template <typename R, typename Req>
  bool sendForResult(ProjectOps &ops, Req req, ResultCallback<R> callback);
  // Forgets the handle: the reply was matched by hand, or the owner reset.
  void clear();
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool RequestHandle::valid() const
{
  return requestId != 0;
}

inline bool TaskRecord::finished() const
{
  return state == TaskState::Completed || state == TaskState::Failed;
}

template <typename Req>
inline std::string taskLabel(const Req &)
{
  return {};
}

template <typename Req>
inline RequestHandle ProjectOps::send(Req req, ReplyCallback callback)
{
  req.requestId = m_nextRequestId++;
  return submit(req.requestId,
      Req::MESSAGE_TYPE,
      protocol::encode(req),
      std::move(callback),
      taskLabel(req));
}

template <typename R, typename Req>
inline RequestHandle ProjectOps::sendForResult(
    Req req, ResultCallback<R> callback)
{
  return send(std::move(req),
      [callback = std::move(callback)](const protocol::ProjectOpReply &reply) {
        if (!callback)
          return;
        callback(
            reply, reply.ok ? protocol::results<R>(reply) : std::optional<R>{});
      });
}

inline bool InFlight::busy(const ProjectOps &ops) const
{
  return handle.valid() && ops.pending(handle);
}

template <typename Req>
inline bool InFlight::send(ProjectOps &ops, Req req, ReplyCallback callback)
{
  if (busy(ops))
    return false;
  handle = ops.send(std::move(req), std::move(callback));
  return true;
}

template <typename R, typename Req>
inline bool InFlight::sendForResult(
    ProjectOps &ops, Req req, ResultCallback<R> callback)
{
  if (busy(ops))
    return false;
  handle = ops.sendForResult<R>(std::move(req), std::move(callback));
  return true;
}

inline void InFlight::clear()
{
  handle = {};
}

} // namespace vsr::scivis_studio::client
