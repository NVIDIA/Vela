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
// (SetPlaying among them), Remote Browse and CancelTask (types 20..58 and
// 61), decoded on the IO thread and queued for the loop thread in arrival
// order.
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

// A sync op that reads or mutates the Project or Scene, and therefore must
// wait until every task the client sent before it has run. Task-launching
// requests (they only queue), CancelTask and Remote Browse are not.
bool waitsForQueuedTasks(const ProjectRequest &request);

// CancelTask and Remote Browse: they touch neither Project nor Scene, so the
// loop may serve one from behind a sync op that is waiting for a queued task
// -- otherwise a queued task could never be cancelled once any sync op
// followed it.
bool independentOfQueuedTasks(const ProjectRequest &request);

/*
 * Runs project requests on the loop thread against the server's
 * ProjectContext. Sync ops call the context, send the ProjectOpReply, then a
 * ProjectSnapshot whenever the Project changed (including "failed" calls that
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
  };

  explicit ProjectOpDispatcher(Host host);

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
  void handle(const protocol::CancelTask &);

  // Sends `reply` after flushing scene pushes (and rebinding the pipeline
  // when asked), then the snapshot when the project changed.
  void finish(
      const protocol::ProjectOpReply &reply, bool projectChanged, bool rebind);
  // An error reply; `projectChanged` when the refused call still mutated.
  void fail(uint64_t requestId,
      const std::string &error,
      bool projectChanged = false);
  // Queues `body` and answers with its task id.
  void startTask(uint64_t requestId, std::string description, TaskBody body);
  // Runs `body`, then -- however it ended -- rebinds (if asked) and flushes
  // scene pushes so the completion report follows them.
  TaskResult runTaskBody(const std::function<TaskResult()> &body, bool rebind);
  void sendSnapshot();

  ProjectContext &context();
  Project &project();

  Host m_host;
};

} // namespace vsr::scivis_studio::server
