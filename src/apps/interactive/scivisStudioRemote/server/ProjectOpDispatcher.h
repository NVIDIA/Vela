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
// RenderShot and CancelTask (types 20..61), decoded on the IO thread and
// queued for the loop thread in arrival order.
using ProjectRequest = std::variant<protocol::NewProject,
    protocol::OpenProject,
    protocol::SaveProject,
    protocol::ImportStaticDataset,
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

// What the loop must know about a request before serving it, one record per
// ProjectRequest alternative (the table in ProjectOpDispatcher.cpp). A sync op
// is {false, true, true}; a task op {true, false, true}, since its dispatch
// only queues and reads the Project when the task runs; RenderShot is
// {true, true, true}, a task whose sync prelude reads the Project; Remote
// Browse and CancelTask are all false; RequestArrayHistogram reads without
// mutating.
struct RequestPolicy
{
  // Answered with a TaskStartedResult and queued on the ServerTaskRunner.
  bool launchesTask{false};
  // Reads the Project or Scene at dispatch, so it must wait until every task
  // the client sent before it has run.
  bool readsProjectAtDispatch{false};
  // Mutates the Project or Scene, or launches a task that would; the render
  // owns both while it is pending, so the request is refused meanwhile.
  bool mutates{false};
};

RequestPolicy policyOf(const ProjectRequest &request);

// The predicates the loop asks, each read off the request's RequestPolicy.

// readsProjectAtDispatch: the request waits for the tasks sent before it.
bool waitsForQueuedTasks(const ProjectRequest &request);

// No flag set: the request touches neither Project nor Scene, so the loop may
// serve it from behind a sync op that is waiting for a queued task --
// otherwise a queued task could never be cancelled once any sync op followed
// it.
bool independentOfQueuedTasks(const ProjectRequest &request);

// How a task is queued: exclusive (the shot render), and what its sync
// prelude did before the task was queued -- a Project change the reply's
// snapshot must show, a rebind of the pipeline.
struct TaskLaunch
{
  bool exclusive{false};
  bool projectChanged{false};
  bool rebind{false};
};

/*
 * Runs project requests on the loop thread against the server's
 * ProjectContext. RenderShot is a Server Task with a sync prelude: the shot
 * becomes the active one, the pipeline rebinds and a snapshot goes out
 * before the task is queued, so the client sees the switch at once; because
 * the prelude reads the Project, RenderShot waits for queued tasks like a
 * sync op does. While the render is queued or running every mutating
 * request is refused with "render in progress" (refuses()) and the
 * body polls its cancel flag once per frame; when the body returns, the
 * host drops the inputs latched meanwhile (Host::dropLatchedInputs) before
 * the ending goes out. Sync ops call the context, send the ProjectOpReply, then
 * a ProjectSnapshot whenever the Project changed (including "failed" calls that
 * still mutate: an import that leaves an ImportFailed record, a load that
 * marks a dataset Unavailable). Task ops reply with a TaskStartedResult and
 * enqueue their body on the ServerTaskRunner; runOneTask() runs one and
 * follows it with the snapshot. At dispatch a task op checks only what does
 * not depend on the Project (the paths it names lie inside the Data Roots);
 * whatever it reads from the Project -- a dataset id, the project's own
 * directory -- it reads when the task runs, because a task queued ahead of
 * it (an OpenProject, say) may still change the Project, and requests must
 * take effect in the order sent. Before any reply the host flushes scene
 * pushes (the TransferScene a project reset asked for), so the client's
 * mirror never lags the snapshot that names its objects, and after an op
 * that changes which shot or camera renders the host rebinds its pipeline.
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
    // The pipeline's camera and renderer follow the active shot again.
    std::function<void()> rebindActiveShot;
    // The UI-state tree stored with the project (ui-state round trip).
    protocol::SubtreePtr *uiState{nullptr};
    // The server is going down: a running render stops at its next frame.
    std::function<bool()> shutdownRequested;
    // The render body has returned: scene edits, SetTime and Pick latched
    // while it held the loop targeted the scene it was mutating and are
    // dropped now, before the ending is sent, so nothing sent in reaction
    // to the ending is lost with them.
    std::function<void()> dropLatchedInputs;
  };

  explicit ProjectOpDispatcher(Host host);

  // A shot render is queued or running.
  bool renderActive() const;

  // True when dispatch() would answer this request with "render in
  // progress": a shot render is queued or running and the request mutates
  // (RequestPolicy::mutates). The loop asks before queueing a request behind
  // the tasks, so a refusal never waits its turn.
  bool refuses(const ProjectRequest &request) const;

  // Serves `request`; a refused one gets its ProjectOpReply error and
  // nothing else happens.
  void dispatch(const ProjectRequest &request);

  // Runs one queued task (if any) and sends the snapshot it earned.
  void runOneTask();

 private:
  // Project
  void handle(const protocol::NewProject &);
  void handle(const protocol::OpenProject &);
  void handle(const protocol::SaveProject &);
  // Datasets
  void handle(const protocol::ImportStaticDataset &);
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

  // Sends `reply` after flushing scene pushes (and rebinding the pipeline
  // when asked), then the snapshot when the project changed.
  void finish(
      const protocol::ProjectOpReply &reply, bool projectChanged, bool rebind);
  // An error reply; `projectChanged` when the refused call still mutated.
  void fail(uint64_t requestId,
      const std::string &error,
      bool projectChanged = false);
  // Queues `body` and answers with its task id.
  void startTask(uint64_t requestId,
      std::string description,
      TaskBody body,
      TaskLaunch launch = {});
  // Runs `body`, then -- however it ended -- rebinds (if asked) and flushes
  // scene pushes so the completion report follows them.
  TaskResult runTaskBody(const std::function<TaskResult()> &body, bool rebind);
  void sendSnapshot();

  ProjectContext &context();
  Project &project();

  Host m_host;
};

} // namespace vsr::scivis_studio::server
