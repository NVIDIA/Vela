// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataRoots.h"
#include "ServerTaskRunner.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "PayloadCommon.h"
#include "PlaybackMessages.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "ShotRigRequests.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
#include "ViewportMessages.h"
// vsr_scivis_studio_model
#include "ProjectContext.h"
// vsr_network
#include "vsr/network/Message.hpp"
// std
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace vsr::scivis_studio::server {

// Every client->server project request the dispatcher serves: Project Ops
// (SetPlaying and RequestArrayHistogram among them), Remote Browse,
// RenderShot and CancelTask (types 20..61, 63), decoded on the IO thread and
// queued for the loop thread in arrival order.
using ProjectRequest = std::variant<protocol::NewProject,
    protocol::OpenProject,
    protocol::SaveProject,
    protocol::ImportStaticDataset,
    protocol::ImportSubtreeDataset,
    protocol::ImportFileAnimationDataset,
    protocol::DeclareFileAnimationDataset,
    protocol::ReimportDataset,
    protocol::RenameDataset,
    protocol::RemoveDataset,
    protocol::LoadDataset,
    protocol::UnloadDataset,
    protocol::RefreshDatasetAvailability,
    protocol::SaveDatasetArchive,
    protocol::LoadDatasetArchive,
    protocol::DiscoverDatasetCandidates,
    protocol::IncorporateDatasetCandidate,
    protocol::CreateShot,
    protocol::RemoveShot,
    protocol::UpdateShot,
    protocol::SetActiveShot,
    protocol::SetPlaying,
    protocol::CreateLightRig,
    protocol::CloneLightRig,
    protocol::RemoveLightRig,
    protocol::RenameLightRig,
    protocol::AddLightToRig,
    protocol::RemoveLightFromRig,
    protocol::CreateCameraRig,
    protocol::RemoveCameraRig,
    protocol::RenameCameraRig,
    protocol::SaveCameraRigArchive,
    protocol::LoadCameraRigArchive,
    protocol::SaveLightRigArchive,
    protocol::LoadLightRigArchive,
    protocol::CreateColorMap,
    protocol::RenameColorMap,
    protocol::RemoveColorMap,
    protocol::ListRoots,
    protocol::ListDirectory,
    protocol::RequestArrayHistogram,
    protocol::RenderShot,
    protocol::CancelTask>;

// True for the message types ProjectRequest covers. Derived from the
// variant's alternatives (their MESSAGE_TYPE), so adding a request means
// adding it there and nowhere else.
bool isProjectRequestType(protocol::StudioMessageType type);

// Decodes `msg` into the alternative its type byte names; empty when the type
// is not a project request or the payload is malformed. Also derived from
// the alternatives.
std::optional<ProjectRequest> decodeProjectRequest(
    const vsr::network::Message &msg);

// What the loop must know about a request before serving it: one kind per
// ProjectRequest alternative (the table in ProjectOpDispatcher.cpp). Two
// facts follow from the kind -- whether the request reads the Project or
// Scene at dispatch, and whether it mutates them (or launches a task that
// would) -- and the predicates below read them off it.
enum class RequestKind
{
  // A sync op: calls the context and replies at dispatch, so it reads the
  // Project then and mutates it (most Project Ops).
  SyncMutating,
  // Reads the Project or Scene at dispatch and mutates nothing
  // (DiscoverDatasetCandidates, RequestArrayHistogram).
  SyncReadOnly,
  // Answered with a TaskStartedResult and queued on the ServerTaskRunner;
  // the dispatch only queues, the task reads and mutates the Project when
  // it runs.
  Task,
  // A task whose sync prelude reads the Project (the shot render).
  RenderShot,
  // Touches neither Project nor Scene (Remote Browse, CancelTask).
  Independent
};

RequestKind kindOf(const ProjectRequest &request);

// The predicates the loop and the dispatcher ask, each read off the kind.

// SyncMutating, SyncReadOnly, RenderShot: the request reads the Project or
// Scene at dispatch, so it waits until every task the client sent before it
// has run.
bool waitsForQueuedTasks(const ProjectRequest &request);

// Independent: the request touches neither Project nor Scene, so the loop may
// serve it from behind a sync op that is waiting for a queued task --
// otherwise a queued task could never be cancelled once any sync op followed
// it.
bool independentOfQueuedTasks(const ProjectRequest &request);

// SyncMutating, Task, RenderShot: the request mutates the Project or Scene,
// or launches a task that would; the render owns both while it is pending,
// so the request is refused meanwhile (ProjectOpDispatcher::refuses).
bool mutatesProject(const ProjectRequest &request);

/*
 * Runs project requests on the loop thread against the server's
 * ProjectContext. Sync ops call the context and send the ProjectOpReply;
 * task ops reply with a TaskStartedResult and enqueue their body on the
 * ServerTaskRunner, which runOneTask() runs one at a time. Neither decides
 * whether a snapshot follows: every mutation the context makes moves its
 * revision() (a "failed" call that still left a mark too -- an import
 * whose ImportFailed record stays, a load that marks a dataset
 * Unavailable), and the loop sends one ProjectSnapshot per revision change
 * after each dispatch and after running a task, so the reply precedes it
 * and a refused or no-op call has none. The same goes for the pipeline: the
 * loop rebinds it when the context's activeShotRevision() moved. RenderShot
 * is a Server Task with a sync prelude: the shot becomes the active one
 * before the task is queued, so the client sees the switch (its snapshot
 * precedes the task's progress); because the prelude reads the Project,
 * RenderShot waits for queued tasks like a sync op does. While the render
 * is queued or running every mutating request is refused with "render in
 * progress" (refuses()) and the body polls its cancel flag once per frame;
 * however the body leaves -- the last frame, a cancel, or a throw out of a
 * frame's load or encode -- the host drops the inputs latched meanwhile
 * (Host::dropLatchedInputs) before the ending goes out. At dispatch a task
 * op checks only what does not depend on the Project (the paths it names
 * lie inside the Data Roots); whatever it reads from the Project -- a
 * dataset id, the project's own directory -- it reads when the task runs,
 * because a task queued ahead of it (an OpenProject, say) may still change
 * the Project, and requests must take effect in the order sent. Before any
 * reply the host flushes scene pushes (the TransferScene a project reset
 * asked for), so the client's mirror never lags the snapshot that names
 * its objects.
 *
 * Example:
 *   ProjectOpDispatcher::Host host;
 *   host.projectContext = &projectContext;
 *   ...
 *   ProjectOpDispatcher dispatcher(host);
 *   dispatcher.dispatch(request); // loop thread
 *   dispatcher.runOneTask();      // once per loop iteration
 */
struct ProjectOpDispatcher
{
  struct Host
  {
    ProjectContext *projectContext{nullptr};
    const DataRoots *dataRoots{nullptr};
    ServerTaskRunner *tasks{nullptr};
    std::function<void(vsr::network::Message &&)> send;
    // Sends the pending TransferScene, if a scene reset requested one.
    std::function<void()> flushScenePushes;
    // The UI-state tree stored with the project (ui-state round trip).
    protocol::SubtreePtr *uiState{nullptr};
    // The server is going down: a running render stops at its next frame.
    std::function<bool()> shutdownRequested;
    // The render body has left, by return or by throw: scene edits, SetTime
    // and Pick latched while it held the loop targeted the scene it was
    // mutating and are dropped now, before the ending is sent, so nothing
    // sent in reaction to the ending is lost with them.
    std::function<void()> dropLatchedInputs;
  };

  explicit ProjectOpDispatcher(Host host);

  // A shot render is queued or running.
  bool renderActive() const;

  // True when dispatch() would answer this request with "render in
  // progress": a shot render is queued or running and the request mutates
  // (mutatesProject). The loop asks before queueing a request behind the
  // tasks, so a refusal never waits its turn.
  bool refuses(const ProjectRequest &request) const;

  // Serves `request`; a refused one gets its ProjectOpReply error and
  // nothing else happens.
  void dispatch(const ProjectRequest &request);

  // Runs one queued task, if any.
  void runOneTask();

 private:
  // One handler per alternative. The task-launching ones (RequestKind::Task
  // and RenderShot) are defined in ProjectOpDispatcherTasks.cpp, the rest in
  // ProjectOpDispatcher.cpp.
  // Project
  void handle(const protocol::NewProject &);
  void handle(const protocol::OpenProject &);
  void handle(const protocol::SaveProject &);
  // Datasets
  void handle(const protocol::ImportStaticDataset &);
  void handle(const protocol::ImportSubtreeDataset &);
  void handle(const protocol::ImportFileAnimationDataset &);
  void handle(const protocol::DeclareFileAnimationDataset &);
  void handle(const protocol::ReimportDataset &);
  void handle(const protocol::RenameDataset &);
  void handle(const protocol::RemoveDataset &);
  void handle(const protocol::LoadDataset &);
  void handle(const protocol::UnloadDataset &);
  void handle(const protocol::RefreshDatasetAvailability &);
  void handle(const protocol::SaveDatasetArchive &);
  void handle(const protocol::LoadDatasetArchive &);
  void handle(const protocol::DiscoverDatasetCandidates &);
  void handle(const protocol::IncorporateDatasetCandidate &);
  // Shots
  void handle(const protocol::CreateShot &);
  void handle(const protocol::RemoveShot &);
  void handle(const protocol::UpdateShot &);
  void handle(const protocol::SetActiveShot &);
  void handle(const protocol::SetPlaying &);
  // Light rigs
  void handle(const protocol::CreateLightRig &);
  void handle(const protocol::CloneLightRig &);
  void handle(const protocol::RemoveLightRig &);
  void handle(const protocol::RenameLightRig &);
  void handle(const protocol::AddLightToRig &);
  void handle(const protocol::RemoveLightFromRig &);
  void handle(const protocol::SaveLightRigArchive &);
  void handle(const protocol::LoadLightRigArchive &);
  // Camera rigs
  void handle(const protocol::CreateCameraRig &);
  void handle(const protocol::RemoveCameraRig &);
  void handle(const protocol::RenameCameraRig &);
  void handle(const protocol::SaveCameraRigArchive &);
  void handle(const protocol::LoadCameraRigArchive &);
  // Color maps
  void handle(const protocol::CreateColorMap &);
  void handle(const protocol::RenameColorMap &);
  void handle(const protocol::RemoveColorMap &);
  // Remote Browse and tasks
  void handle(const protocol::ListRoots &);
  void handle(const protocol::ListDirectory &);
  void handle(const protocol::RenderShot &);
  void handle(const protocol::CancelTask &);
  // Viewport
  void handle(const protocol::RequestArrayHistogram &);

  // Sends `reply` after flushing scene pushes.
  void finish(const protocol::ProjectOpReply &reply);
  // An error reply.
  void fail(uint64_t requestId, const std::string &error);
  // Queues `body` (exclusive: the shot render) and answers with its task id.
  void startTask(uint64_t requestId,
      std::string description,
      TaskBody body,
      bool exclusive = false);
  // Runs `body`, then -- however it ended -- flushes scene pushes so the
  // completion report follows them.
  TaskResult runTaskBody(const std::function<TaskResult()> &body);

  ProjectContext &context();
  Project &project();

  Host m_host;
};

} // namespace vsr::scivis_studio::server
