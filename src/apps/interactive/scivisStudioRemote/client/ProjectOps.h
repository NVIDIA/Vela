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
// vsr_scivis_studio_model
#include "Shot.h"
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
  // Frames a render wrote, from TaskCompleted or TaskFailed (a cancelled or
  // failed render leaves its partial frames on disk).
  uint64_t framesCompleted{0};
  // Launched by this client's RenderShot: the editors show a note while it
  // is active, since the server refuses edits until the render ends.
  bool render{false};
  // Counts the times the record started over: a TaskStarted reply, or
  // progress for a record that finished, names a new task under a reused id
  // (a restarted server counts from 1 again), so the record is reset as if
  // newly heard of. Lets a watcher tell a new task's ending from a repeat.
  uint32_t generation{0};
  // The user has already been told of this state, so it must not toast: set
  // with the client's own "connection lost" failure at BootstrapBegin (the
  // banner says it), cleared as soon as the server speaks of the task again
  // (the replay's ending overwrites the record, its progress starts it over).
  bool announced{false};

  bool finished() const;
  // The one-line toast for a finished record: "<label> completed (N frames):
  // <outcome>" or "<label> failed after N frames: <error>", each part only
  // when the record has it ("<task>" stands in for a missing label). Empty
  // unless finished().
  std::string describeEnding() const;
};

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
 * The typed wrappers cover every request type so UI code never builds a
 * payload by hand. Ops whose reply carries a *Result payload take a
 * ResultCallback<R>; task-launching ops report TaskStartedResult and register
 * the task, labelled after the request, in tasks(). A Pick shares the id
 * space but is answered by a plain PickReply, so it has its own callback
 * type but shares the pending list, so pending(), forget() and the
 * connection-loss failure cover it all the same; a server Error naming a
 * Pick ("Pick 7 refused: ...") fails the oldest pending one.
 *
 * Example:
 *   auto &ops = connection.projectOps();
 *   ops.createShot("Shot 2", [&](const ProjectOpReply &reply,
 *                                const std::optional<ShotCreatedResult> &r) {
 *     if (reply.ok) selectShot(r->shotId);
 *     else showError(reply.error);
 *   });
 *   ops.openProject("/data/run7", [](const auto &, const auto &) {});
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

  // Mints req.requestId, sends, and stores the callback under that id.
  // `taskLabel` names the TaskRecord if the reply starts a task.
  template <typename Req>
  RequestHandle send(
      Req req, ReplyCallback callback, std::string taskLabel = {});
  // send() decoding the reply's results as an R for the callback.
  template <typename R, typename Req>
  RequestHandle sendForResult(
      Req req, ResultCallback<R> callback, std::string taskLabel = {});

  // Project ops and picks awaiting their reply.
  size_t pendingCount() const;
  bool pending(RequestHandle handle) const;
  // Drops the callback; the reply, if it comes, is still used for task
  // tracking and otherwise ignored. A pick is retired outright: the server
  // answers only the surviving Pick, so nothing would ever retire it.
  void forget(RequestHandle handle);

  // Project (20..22) /////////////////////////////////////////////////////////

  RequestHandle newProject(ReplyCallback callback);
  RequestHandle openProject(const std::filesystem::path &directory,
      ResultCallback<protocol::TaskStartedResult> callback);
  // Absent directory saves in place; uiState may be null.
  RequestHandle saveProject(
      const std::optional<std::filesystem::path> &directory,
      protocol::SubtreePtr uiState,
      ResultCallback<protocol::TaskStartedResult> callback);

  // Datasets (23..35) ////////////////////////////////////////////////////////

  RequestHandle importStaticDataset(const std::string &name,
      const std::filesystem::path &sourcePath,
      vsr::io::ImporterType importerType,
      bool fromSubtreeArchive,
      ResultCallback<protocol::TaskStartedResult> callback);
  RequestHandle importFileAnimationDataset(const std::string &name,
      const std::vector<std::filesystem::path> &sourcePaths,
      vsr::io::ImporterType importerType,
      bool setActiveShotFrameCount,
      ResultCallback<protocol::TaskStartedResult> callback);
  RequestHandle declareFileAnimationDataset(const std::string &name,
      const std::vector<std::string> &sourceList,
      vsr::io::ImporterType importerType,
      bool setActiveShotFrameCount,
      ResultCallback<protocol::DatasetCreatedResult> callback);
  RequestHandle reimportDataset(const DatasetID &datasetId,
      ResultCallback<protocol::TaskStartedResult> callback);
  RequestHandle renameDataset(const DatasetID &datasetId,
      const std::string &newName,
      ReplyCallback callback);
  RequestHandle removeDataset(
      const DatasetID &datasetId, bool keepAssetFile, ReplyCallback callback);
  RequestHandle loadDataset(const DatasetID &datasetId,
      ResultCallback<protocol::TaskStartedResult> callback);
  RequestHandle unloadDataset(
      const DatasetID &datasetId, ReplyCallback callback);
  RequestHandle refreshDatasetAvailability(
      const DatasetID &datasetId, ReplyCallback callback);
  RequestHandle saveDatasetArchive(const DatasetID &datasetId,
      const std::filesystem::path &file,
      ResultCallback<protocol::TaskStartedResult> callback);
  RequestHandle loadDatasetArchive(const std::filesystem::path &file,
      ResultCallback<protocol::TaskStartedResult> callback);
  RequestHandle discoverDatasetCandidates(
      ResultCallback<protocol::DiscoverDatasetCandidatesResult> callback);
  RequestHandle incorporateDatasetCandidate(const std::filesystem::path &file,
      const std::string &proposedName,
      const std::string &name,
      ResultCallback<protocol::TaskStartedResult> callback);

  // Shots (36..39) ///////////////////////////////////////////////////////////

  // An empty name is numbered by the server ("Shot N").
  RequestHandle createShot(const std::string &name,
      ResultCallback<protocol::ShotCreatedResult> callback);
  RequestHandle removeShot(const ShotID &shotId, ReplyCallback callback);
  // The whole Shot; the server validates and replaces its copy (never
  // honouring `playing`).
  RequestHandle updateShot(const Shot &shot, ReplyCallback callback);
  RequestHandle setActiveShot(const ShotID &shotId, ReplyCallback callback);

  // Light rigs (40..45, 51..52) //////////////////////////////////////////////

  RequestHandle createLightRig(const std::string &name,
      ResultCallback<protocol::LightRigCreatedResult> callback);
  RequestHandle cloneLightRig(const LightRigID &lightRigId,
      ResultCallback<protocol::LightRigCreatedResult> callback);
  RequestHandle removeLightRig(
      const LightRigID &lightRigId, ReplyCallback callback);
  RequestHandle renameLightRig(const LightRigID &lightRigId,
      const std::string &newName,
      ReplyCallback callback);
  // subtype is the ANARI light subtype ("directional", "point", ...).
  RequestHandle addLightToRig(const LightRigID &lightRigId,
      const std::string &subtype,
      ResultCallback<protocol::LightAddedResult> callback);
  RequestHandle removeLightFromRig(const LightRigID &lightRigId,
      const SceneNodeRef &lightNode,
      ReplyCallback callback);
  RequestHandle saveLightRigArchive(const LightRigID &lightRigId,
      const std::filesystem::path &file,
      ReplyCallback callback);
  RequestHandle loadLightRigArchive(const std::filesystem::path &file,
      ResultCallback<protocol::LightRigCreatedResult> callback);

  // Camera rigs (46..50) /////////////////////////////////////////////////////

  RequestHandle createCameraRig(const std::string &name,
      ResultCallback<protocol::CameraRigCreatedResult> callback);
  RequestHandle removeCameraRig(
      const CameraRigID &cameraRigId, ReplyCallback callback);
  RequestHandle renameCameraRig(const CameraRigID &cameraRigId,
      const std::string &newName,
      ReplyCallback callback);
  RequestHandle saveCameraRigArchive(const CameraRigID &cameraRigId,
      const std::filesystem::path &file,
      ReplyCallback callback);
  RequestHandle loadCameraRigArchive(const std::filesystem::path &file,
      ResultCallback<protocol::CameraRigCreatedResult> callback);

  // Color maps (53..55) //////////////////////////////////////////////////////

  RequestHandle createColorMap(const std::string &name,
      ResultCallback<protocol::ColorMapCreatedResult> callback);
  RequestHandle renameColorMap(const ColorMapID &colorMapId,
      const std::string &newName,
      ReplyCallback callback);
  RequestHandle removeColorMap(
      const ColorMapID &colorMapId, ReplyCallback callback);

  // Remote Browse (56..57) ///////////////////////////////////////////////////

  RequestHandle listRoots(ResultCallback<protocol::ListRootsResult> callback);
  RequestHandle listDirectory(const std::filesystem::path &directory,
      ResultCallback<protocol::ListDirectoryResult> callback);

  // Playback (58) ////////////////////////////////////////////////////////////

  // shotId must be the active shot; the server confirms with a snapshot.
  RequestHandle setPlaying(
      const ShotID &shotId, bool playing, ReplyCallback callback);

  // Offline render (60) //////////////////////////////////////////////////////

  // Makes the shot active and renders its frames as a Server Task with
  // determinate progress; refused unless the project is saved and no render
  // is queued or running. The record is flagged `render`.
  RequestHandle renderShot(const ShotID &shotId,
      ResultCallback<protocol::TaskStartedResult> callback);

  // Array histogram (59) /////////////////////////////////////////////////////

  // Sync on the server: scalar element types only, binCount clamped to
  // [1, 4096] there.
  RequestHandle requestArrayHistogram(const SceneObjectRef &array,
      uint32_t binCount,
      ResultCallback<protocol::ArrayHistogramResult> callback);

  // Pick (62) ////////////////////////////////////////////////////////////////

  // x right, y down from the top-left of the frame, in frame-header pixels.
  RequestHandle pick(int x, int y, PickCallback callback);

  // Server Tasks (61) ////////////////////////////////////////////////////////

  // Cooperative: removes a queued task; the running one is stopped at its
  // next frame boundary if it is a render, refused otherwise (the server
  // decides).
  RequestHandle cancelTask(uint64_t taskId, ReplyCallback callback);

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
  // `error`, already `announced`; the bootstrap's task-status replay then
  // overwrites the ones the server still knows about like any other event.
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
  // else kept), its generation bumped when there was one: a new task under
  // the id.
  TaskRecord &startOver(uint64_t taskId);
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
inline RequestHandle ProjectOps::send(
    Req req, ReplyCallback callback, std::string taskLabel)
{
  req.requestId = m_nextRequestId++;
  return submit(req.requestId,
      Req::MESSAGE_TYPE,
      protocol::encode(req),
      std::move(callback),
      std::move(taskLabel));
}

template <typename R, typename Req>
inline RequestHandle ProjectOps::sendForResult(
    Req req, ResultCallback<R> callback, std::string taskLabel)
{
  return send(
      std::move(req),
      [callback = std::move(callback)](const protocol::ProjectOpReply &reply) {
        if (!callback)
          return;
        callback(
            reply, reply.ok ? protocol::results<R>(reply) : std::optional<R>{});
      },
      std::move(taskLabel));
}

} // namespace vsr::scivis_studio::client
