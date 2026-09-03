// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOpDispatcher.h"
#include "ArrayHistogram.h"
#include "RemoteBrowse.h"
// vsr_scivis_studio_protocol
#include "ProjectSnapshot.h"
#include "StudioCodec.h"
// vsr_scivis_studio_model
#include "ProjectPersistence.h"
#include "RenderShot.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace vsr::scivis_studio::server {

using namespace protocol;
using vsr::network::Message;

namespace {

template <typename T>
std::optional<ProjectRequest> decodeAs(const Message &msg)
{
  auto payload = decode<T>(msg);
  if (!payload)
    return {};
  return ProjectRequest{std::move(*payload)};
}

// The failure of a task body whose dataset is gone by the time it runs.
TaskResult datasetNotFound()
{
  TaskResult result;
  result.ok = false;
  result.error = "dataset not found";
  return result;
}

// What an import task reports for the dataset the context handed back: null
// is a refusal; a record that is not Available is a failed import, which
// still changed the project (the ImportFailed record stays in it).
TaskResult importResult(const Dataset *dataset)
{
  TaskResult result;
  if (!dataset) {
    result.ok = false;
    result.error = "import failed";
    return result;
  }
  result.projectChanged = true;
  result.message = dataset->id;
  if (dataset->status != DatasetStatus::Available) {
    result.ok = false;
    result.error = "dataset '" + dataset->name + "' import failed ("
        + dataset::displayStatus(*dataset) + "); see the server log";
  }
  return result;
}

// The status `id` has, or Unavailable for a dataset that is not there; the
// before/after pair tells whether a failed load or unload still changed it.
DatasetStatus statusOf(const Project &project, const DatasetID &id)
{
  const auto *dataset = project::findDataset(project, id);
  return dataset ? dataset->status : DatasetStatus::Unavailable;
}

} // namespace

// Request plumbing ///////////////////////////////////////////////////////////

namespace {

template <size_t I>
using RequestAlternative = std::variant_alternative_t<I, ProjectRequest>;

// The alternatives are the one list of request types: each carries its
// MESSAGE_TYPE, so both the type test and the decoder fold over them.
template <size_t... I>
bool isProjectRequestTypeOf(StudioMessageType type, std::index_sequence<I...>)
{
  return ((RequestAlternative<I>::MESSAGE_TYPE == type) || ...);
}

template <size_t... I>
std::optional<ProjectRequest> decodeProjectRequestOf(
    const Message &msg, StudioMessageType type, std::index_sequence<I...>)
{
  std::optional<ProjectRequest> decoded;
  const auto tryOne = [&](auto tag) {
    using T = RequestAlternative<decltype(tag)::value>;
    if (T::MESSAGE_TYPE != type)
      return false;
    decoded = decodeAs<T>(msg);
    return true;
  };
  (tryOne(std::integral_constant<size_t, I>{}) || ...);
  return decoded;
}

constexpr auto REQUEST_ALTERNATIVES =
    std::make_index_sequence<std::variant_size_v<ProjectRequest>>{};

} // namespace

bool isProjectRequestType(StudioMessageType type)
{
  return isProjectRequestTypeOf(type, REQUEST_ALTERNATIVES);
}

std::optional<ProjectRequest> decodeProjectRequest(const Message &msg)
{
  const auto type = messageType(msg);
  if (!type)
    return {};
  return decodeProjectRequestOf(msg, *type, REQUEST_ALTERNATIVES);
}

// Request policy /////////////////////////////////////////////////////////////

namespace {

// Only the rows below exist: a ProjectRequest alternative without one fails
// to compile here, so adding an alternative means adding its row.
template <typename Request>
constexpr RequestPolicy policyOf()
{
  static_assert(sizeof(Request) == 0,
      "no RequestPolicy row for this ProjectRequest alternative");
  return {};
}

#define VSR_REQUEST_POLICY(Request, launches, reads, mutates)                  \
  template <>                                                                  \
  constexpr RequestPolicy policyOf<Request>()                                  \
  {                                                                            \
    return {launches, reads, mutates};                                         \
  }

// clang-format off
// One row per ProjectRequest alternative, in the variant's order:
// launchesTask, readsProjectAtDispatch, mutates.
//                                                 launches reads  mutates
// Project
VSR_REQUEST_POLICY(NewProject,                     false,   true,  true)
VSR_REQUEST_POLICY(OpenProject,                    true,    false, true)
VSR_REQUEST_POLICY(SaveProject,                    true,    false, true)
// Datasets
VSR_REQUEST_POLICY(ImportStaticDataset,            true,    false, true)
VSR_REQUEST_POLICY(ImportFileAnimationDataset,     true,    false, true)
VSR_REQUEST_POLICY(DeclareFileAnimationDataset,    false,   true,  true)
VSR_REQUEST_POLICY(ReimportDataset,                true,    false, true)
VSR_REQUEST_POLICY(RenameDataset,                  false,   true,  true)
VSR_REQUEST_POLICY(RemoveDataset,                  false,   true,  true)
VSR_REQUEST_POLICY(LoadDataset,                    true,    false, true)
VSR_REQUEST_POLICY(UnloadDataset,                  false,   true,  true)
VSR_REQUEST_POLICY(RefreshDatasetAvailability,     false,   true,  true)
VSR_REQUEST_POLICY(SaveDatasetArchive,             true,    false, true)
VSR_REQUEST_POLICY(LoadDatasetArchive,             true,    false, true)
VSR_REQUEST_POLICY(DiscoverDatasetCandidates,      false,   true,  true)
VSR_REQUEST_POLICY(IncorporateDatasetCandidate,    true,    false, true)
// Shots
VSR_REQUEST_POLICY(CreateShot,                     false,   true,  true)
VSR_REQUEST_POLICY(RemoveShot,                     false,   true,  true)
VSR_REQUEST_POLICY(UpdateShot,                     false,   true,  true)
VSR_REQUEST_POLICY(SetActiveShot,                  false,   true,  true)
VSR_REQUEST_POLICY(SetPlaying,                     false,   true,  true)
// Light rigs
VSR_REQUEST_POLICY(CreateLightRig,                 false,   true,  true)
VSR_REQUEST_POLICY(CloneLightRig,                  false,   true,  true)
VSR_REQUEST_POLICY(RemoveLightRig,                 false,   true,  true)
VSR_REQUEST_POLICY(RenameLightRig,                 false,   true,  true)
VSR_REQUEST_POLICY(AddLightToRig,                  false,   true,  true)
VSR_REQUEST_POLICY(RemoveLightFromRig,             false,   true,  true)
// Camera rigs
VSR_REQUEST_POLICY(CreateCameraRig,                false,   true,  true)
VSR_REQUEST_POLICY(RemoveCameraRig,                false,   true,  true)
VSR_REQUEST_POLICY(RenameCameraRig,                false,   true,  true)
VSR_REQUEST_POLICY(SaveCameraRigArchive,           false,   true,  true)
VSR_REQUEST_POLICY(LoadCameraRigArchive,           false,   true,  true)
VSR_REQUEST_POLICY(SaveLightRigArchive,            false,   true,  true)
VSR_REQUEST_POLICY(LoadLightRigArchive,            false,   true,  true)
// Color maps
VSR_REQUEST_POLICY(CreateColorMap,                 false,   true,  true)
VSR_REQUEST_POLICY(RenameColorMap,                 false,   true,  true)
VSR_REQUEST_POLICY(RemoveColorMap,                 false,   true,  true)
// Remote Browse, viewport and tasks
VSR_REQUEST_POLICY(ListRoots,                      false,   false, false)
VSR_REQUEST_POLICY(ListDirectory,                  false,   false, false)
VSR_REQUEST_POLICY(RequestArrayHistogram,          false,   true,  false)
VSR_REQUEST_POLICY(RenderShot,                     true,    true,  true)
VSR_REQUEST_POLICY(CancelTask,                     false,   false, false)
// clang-format on

#undef VSR_REQUEST_POLICY

} // namespace

RequestPolicy policyOf(const ProjectRequest &request)
{
  return std::visit(
      [](const auto &r) { return policyOf<std::decay_t<decltype(r)>>(); },
      request);
}

bool waitsForQueuedTasks(const ProjectRequest &request)
{
  return policyOf(request).readsProjectAtDispatch;
}

bool independentOfQueuedTasks(const ProjectRequest &request)
{
  const auto policy = policyOf(request);
  return !policy.launchesTask && !policy.readsProjectAtDispatch
      && !policy.mutates;
}

// Dispatcher /////////////////////////////////////////////////////////////////

ProjectOpDispatcher::ProjectOpDispatcher(Host host) : m_host(std::move(host)) {}

void ProjectOpDispatcher::dispatch(const ProjectRequest &request)
{
  if (refuses(request)) {
    // Pause-and-refuse: the render owns the Project and the Scene until it
    // ends; whoever asked can try again afterwards.
    const auto requestId =
        std::visit([](const auto &r) { return r.requestId; }, request);
    fail(requestId, "render in progress");
    return;
  }
  std::visit([this](const auto &r) { handle(r); }, request);
}

bool ProjectOpDispatcher::refuses(const ProjectRequest &request) const
{
  return renderActive() && policyOf(request).mutates;
}

void ProjectOpDispatcher::runOneTask()
{
  const auto ran = m_host.tasks->runOne();
  if (ran && ran->result.projectChanged)
    sendSnapshot();
}

bool ProjectOpDispatcher::renderActive() const
{
  return m_host.tasks->exclusivePending();
}

ProjectContext &ProjectOpDispatcher::context()
{
  return *m_host.projectContext;
}

Project &ProjectOpDispatcher::project()
{
  return context().project();
}

void ProjectOpDispatcher::finish(
    const ProjectOpReply &reply, bool projectChanged, bool rebind)
{
  if (rebind)
    m_host.rebindActiveShot();
  m_host.flushScenePushes();
  m_host.send(encode(reply));
  if (projectChanged)
    sendSnapshot();
}

void ProjectOpDispatcher::fail(
    uint64_t requestId, const std::string &error, bool projectChanged)
{
  vsr::core::logWarning("[StudioServer] request %llu refused: %s",
      static_cast<unsigned long long>(requestId),
      error.c_str());
  finish(makeErrorReply(requestId, error), projectChanged, false);
}

void ProjectOpDispatcher::startTask(uint64_t requestId,
    std::string description,
    TaskBody body,
    TaskLaunch launch)
{
  const auto taskId = m_host.tasks->enqueue(
      std::move(description), std::move(body), launch.exclusive);
  auto reply = makeOkReply(requestId);
  setResults(reply, TaskStartedResult{taskId});
  finish(reply, launch.projectChanged, launch.rebind);
}

TaskResult ProjectOpDispatcher::runTaskBody(
    const std::function<TaskResult()> &body, bool rebind)
{
  // The rebind and the flush follow the body whatever its outcome: a failed
  // (or throwing) open may have reset the scene already, and the pipeline
  // must not keep handles into the scene that was.
  const auto follow = [&] {
    if (rebind)
      m_host.rebindActiveShot();
    m_host.flushScenePushes();
  };
  TaskResult result;
  try {
    result = body();
  } catch (...) {
    follow();
    throw;
  }
  follow();
  return result;
}

void ProjectOpDispatcher::sendSnapshot()
{
  m_host.send(encode(ProjectSnapshot{project()}));
}

// Project ////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const NewProject &req)
{
  context().createUnsavedProject();
  *m_host.uiState = nullptr;
  finish(makeOkReply(req.requestId), true, true);
}

void ProjectOpDispatcher::handle(const OpenProject &req)
{
  std::string error;
  const auto directory = m_host.dataRoots->resolve(req.directory, &error);
  if (!directory) {
    fail(req.requestId, error);
    return;
  }

  startTask(req.requestId,
      "open project '" + directory->string() + "'",
      [this, directory = *directory](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              progress("staging");
              ProjectOpenStage stage;
              if (!stageProjectOpen(directory, stage, {}, &result.error)) {
                result.ok = false;
                return result;
              }
              progress("applying");
              auto uiState = makeSubtree();
              if (!context().openStagedProject(
                      stage, &uiState->root(), &result.error)) {
                result.ok = false;
                return result;
              }
              *m_host.uiState = uiState;
              // The opened project's layout reaches the client that asked
              // before the snapshot that follows the task; a bootstrap
              // carries it too.
              m_host.send(encode(UIState{*m_host.uiState}));
              result.projectChanged = true;
              return result;
            },
            true);
      });
}

void ProjectOpDispatcher::handle(const SaveProject &req)
{
  // Only a named directory can be checked now; "the project's own" is read
  // when the task runs, since an OpenProject queued ahead may change it.
  std::optional<std::filesystem::path> named;
  if (req.directory) {
    std::string error;
    named = m_host.dataRoots->resolve(*req.directory, &error);
    if (!named) {
      fail(req.requestId, error);
      return;
    }
  }

  startTask(req.requestId,
      named ? "save project to '" + named->string() + "'"
            : std::string("save project"),
      [this, named, uiState = req.uiState](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              auto directory = named;
              if (!directory) {
                const auto &own = project().projectDirectory;
                if (own.empty()) {
                  result.ok = false;
                  result.error =
                      "project has never been saved; choose a directory";
                  return result;
                }
                directory = m_host.dataRoots->resolve(own, &result.error);
                if (!directory) {
                  result.ok = false;
                  return result;
                }
              }
              progress("writing");
              // A save without UI state keeps what the project opened with,
              // so a headless save never drops the user's layout.
              const auto &tree = uiState ? uiState : *m_host.uiState;
              if (!context().saveProject(*directory,
                      tree ? &tree->root() : nullptr,
                      &result.error)) {
                result.ok = false;
                return result;
              }
              if (uiState)
                *m_host.uiState = uiState;
              result.projectChanged = true;
              return result;
            },
            false);
      });
}

// Datasets ///////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const ImportStaticDataset &req)
{
  std::string error;
  const auto source = m_host.dataRoots->resolve(req.sourcePath, &error);
  if (!source) {
    fail(req.requestId, error);
    return;
  }

  startTask(req.requestId,
      "import '" + source->string() + "'",
      [this, req, source = *source](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              progress("importing");
              return importResult(req.fromSubtreeArchive
                      ? context().addStaticDatasetFromSubtree(req.name, source)
                      : context().addStaticDataset(
                            req.name, source, req.importerType));
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const ImportFileAnimationDataset &req)
{
  if (req.sourcePaths.empty()) {
    fail(req.requestId, "no source paths given");
    return;
  }
  std::vector<std::filesystem::path> sources;
  for (const auto &path : req.sourcePaths) {
    std::string error;
    const auto source = m_host.dataRoots->resolve(path, &error);
    if (!source) {
      fail(req.requestId, error);
      return;
    }
    sources.push_back(*source);
  }

  startTask(req.requestId,
      "import " + std::to_string(sources.size()) + " animation frame(s) from '"
          + sources.front().string() + "'",
      [this, req, sources = std::move(sources)](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              progress("importing");
              FileAnimationDatasetOptions options;
              options.setActiveShotFrameCount = req.setActiveShotFrameCount;
              return importResult(context().addFileAnimationDataset(
                  req.name, sources, req.importerType, options));
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const DeclareFileAnimationDataset &req)
{
  if (req.sourceList.empty()) {
    fail(req.requestId, "source list is empty");
    return;
  }
  FileAnimationDatasetOptions options;
  options.setActiveShotFrameCount = req.setActiveShotFrameCount;
  auto *dataset = context().addDeclaredFileAnimationDataset(
      req.name, req.sourceList, req.importerType, options);
  if (!dataset) {
    fail(req.requestId, "dataset could not be declared");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, DatasetCreatedResult{dataset->id});
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const ReimportDataset &req)
{
  startTask(req.requestId,
      "reimport dataset '" + req.datasetId + "'",
      [this, id = req.datasetId](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              if (!project::findDataset(project(), id))
                return datasetNotFound();
              progress("reimporting");
              result.ok = context().reimportStaticDataset(id, &result.error);
              result.projectChanged = result.ok;
              return result;
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const RenameDataset &req)
{
  std::string error;
  if (!context().renameDataset(req.datasetId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RemoveDataset &req)
{
  std::string error;
  if (!context().removeDataset(req.datasetId, req.keepAssetFile, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const LoadDataset &req)
{
  startTask(req.requestId,
      "load dataset '" + req.datasetId + "'",
      [this, id = req.datasetId](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              if (!project::findDataset(project(), id))
                return datasetNotFound();
              progress("loading");
              const auto statusBefore = statusOf(project(), id);
              result.ok = context().loadDataset(id, &result.error);
              // A failed load marks the dataset Unavailable: still a change.
              result.projectChanged =
                  result.ok || statusOf(project(), id) != statusBefore;
              return result;
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const UnloadDataset &req)
{
  const auto statusBefore = statusOf(project(), req.datasetId);
  std::string error;
  if (!context().unloadDataset(req.datasetId, &error)) {
    // A refused unload may still have re-marked the dataset.
    fail(req.requestId,
        error,
        statusOf(project(), req.datasetId) != statusBefore);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RefreshDatasetAvailability &req)
{
  auto *dataset = project::findDataset(project(), req.datasetId);
  if (!dataset) {
    fail(req.requestId, "dataset not found");
    return;
  }
  const auto before = dataset->status;
  context().refreshUnloadedDatasetAvailability(*dataset);
  finish(makeOkReply(req.requestId), dataset->status != before, false);
}

void ProjectOpDispatcher::handle(const SaveDatasetArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  startTask(req.requestId,
      "save dataset '" + req.datasetId + "' to '" + file->string() + "'",
      [this, id = req.datasetId, file = *file](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              if (!project::findDataset(project(), id))
                return datasetNotFound();
              progress("writing");
              result.ok = context().saveDatasetArchive(id, file, &result.error);
              return result; // the project itself is untouched
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const LoadDatasetArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  startTask(req.requestId,
      "load dataset archive '" + file->string() + "'",
      [this, file = *file](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              progress("loading");
              auto *dataset = context().loadDatasetArchive(file, &result.error);
              result.ok = dataset != nullptr;
              if (dataset) {
                result.message = dataset->id;
                result.projectChanged = true;
              }
              return result;
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const DiscoverDatasetCandidates &req)
{
  DiscoverDatasetCandidatesResult candidates;
  for (const auto &candidate : context().discoverDatasetCandidates())
    candidates.candidates.push_back({candidate.file, candidate.proposedName});
  auto reply = makeOkReply(req.requestId);
  setResults(reply, candidates);
  finish(reply, false, false);
}

void ProjectOpDispatcher::handle(const IncorporateDatasetCandidate &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  // The candidate keeps the path as discovered: incorporate compares it with
  // the project's datasets directory to tell a managed file from a rename.
  const DatasetCandidate candidate{req.file, req.proposedName};
  startTask(req.requestId,
      "incorporate dataset candidate '" + file->string() + "'",
      [this, candidate, name = req.name](const TaskControl &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              progress("loading");
              auto *dataset = context().incorporateDatasetCandidate(
                  candidate, name, &result.error);
              result.ok = dataset != nullptr;
              if (dataset) {
                result.message = dataset->id;
                result.projectChanged = true;
              }
              return result;
            },
            false);
      });
}

// Shots //////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateShot &req)
{
  if (!context().addShot(req.name) || project().shots.empty()) {
    fail(req.requestId, "shot could not be created");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, ShotCreatedResult{project().shots.back().id});
  finish(reply, true, true); // the new shot is the active one
}

void ProjectOpDispatcher::handle(const RemoveShot &req)
{
  const bool wasActive = project().activeShotId == req.shotId;
  std::string error;
  if (!context().removeShot(req.shotId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, wasActive);
}

void ProjectOpDispatcher::handle(const UpdateShot &req)
{
  std::string error;
  if (!context().updateShot(req.shot, &error)) {
    fail(req.requestId, error);
    return;
  }
  const bool isActive = project().activeShotId == req.shot.id;
  finish(makeOkReply(req.requestId), true, isActive);
}

void ProjectOpDispatcher::handle(const SetActiveShot &req)
{
  std::string error;
  if (!context().setActiveShot(req.shotId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, true);
}

// Play/pause is a confirmed mutation: the reply says it took, the snapshot
// carries playing plus the frame time rests on (currentFrame). The ticking
// itself belongs to the server loop.
void ProjectOpDispatcher::handle(const SetPlaying &req)
{
  std::string error;
  if (!context().setPlaying(req.shotId, req.playing, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

// Light rigs /////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateLightRig &req)
{
  auto *rig = context().createLightRig(req.name);
  if (!rig) {
    fail(req.requestId, "light rig could not be created");
    return;
  }
  // A new rig is bound to no shot: hide it like every other unbound rig.
  context().applyActiveShot();
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightRigCreatedResult{rig->id});
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const CloneLightRig &req)
{
  if (!light_rig::findLightRig(project(), req.lightRigId)) {
    fail(req.requestId, "light rig not found");
    return;
  }
  auto *clone = context().cloneLightRig(req.lightRigId);
  if (!clone) {
    fail(req.requestId, "light rig could not be cloned");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightRigCreatedResult{clone->id});
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RemoveLightRig &req)
{
  if (!context().removeLightRig(req.lightRigId)) {
    fail(req.requestId, "light rig not found");
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RenameLightRig &req)
{
  std::string error;
  if (!context().renameLightRig(req.lightRigId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const AddLightToRig &req)
{
  auto *rig = light_rig::findLightRig(project(), req.lightRigId);
  if (!rig) {
    fail(req.requestId, "light rig not found");
    return;
  }
  if (!light_rig::isKnownLightSubtype(req.subtype)) {
    fail(req.requestId, "unknown light subtype '" + req.subtype + "'");
    return;
  }
  auto node = context().addLightToRig(*rig, req.subtype);
  if (!node) {
    fail(req.requestId, "light rig has no scene node");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightAddedResult{context().refFor("studio", node)});
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RemoveLightFromRig &req)
{
  auto *rig = light_rig::findLightRig(project(), req.lightRigId);
  if (!rig) {
    fail(req.requestId, "light rig not found");
    return;
  }
  auto node = context().resolve(req.lightNode);
  if (!node || !context().removeLightFromRig(*rig, node)) {
    fail(req.requestId, "node is not a light of that rig");
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const SaveLightRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  if (!context().saveLightRigArchive(req.lightRigId, *file, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), false, false);
}

void ProjectOpDispatcher::handle(const LoadLightRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  auto *rig = context().loadLightRigArchive(*file, &error);
  if (!rig) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightRigCreatedResult{rig->id});
  finish(reply, true, false);
}

// Camera rigs ////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateCameraRig &req)
{
  auto *rig = context().createCameraRig(req.name);
  if (!rig) {
    fail(req.requestId, "camera rig could not be created");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, CameraRigCreatedResult{rig->id});
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RemoveCameraRig &req)
{
  if (!context().removeCameraRig(req.cameraRigId)) {
    fail(req.requestId, "camera rig not found");
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RenameCameraRig &req)
{
  std::string error;
  if (!context().renameCameraRig(req.cameraRigId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const SaveCameraRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  if (!context().saveCameraRigArchive(req.cameraRigId, *file, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), false, false);
}

void ProjectOpDispatcher::handle(const LoadCameraRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  auto *rig = context().loadCameraRigArchive(*file, &error);
  if (!rig) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, CameraRigCreatedResult{rig->id});
  finish(reply, true, false);
}

// Color maps /////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateColorMap &req)
{
  auto *record = context().createColorMap(req.name);
  if (!record) {
    fail(req.requestId, "color map could not be created");
    return;
  }
  ColorMapCreatedResult result;
  result.colorMapId = record->id;
  if (auto array = context().resolveColorMapArray(record->id))
    result.object = {array->type(), array.index()};
  auto reply = makeOkReply(req.requestId);
  setResults(reply, result);
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RenameColorMap &req)
{
  std::string error;
  if (!context().renameColorMap(req.colorMapId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RemoveColorMap &req)
{
  std::string error;
  if (!context().removeColorMap(req.colorMapId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

// Remote Browse and tasks ////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const ListRoots &req)
{
  auto reply = makeOkReply(req.requestId);
  setResults(reply, listRoots(*m_host.dataRoots));
  finish(reply, false, false);
}

void ProjectOpDispatcher::handle(const ListDirectory &req)
{
  ListDirectoryResult listing;
  std::string error;
  if (!listDirectory(*m_host.dataRoots, req.directory, listing, &error)) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, listing);
  finish(reply, false, false);
}

// The offline render. Sync prelude first (the shot becomes active, the
// pipeline rebinds, the snapshot shows it), then the exclusive task whose
// body walks the frames, reporting each and stopping at the next boundary
// once a cancel or the shutdown is asked for. Partial frames stay on disk;
// the ending carries how many were written. dispatch() has already refused
// this request when a render is pending, and the host held it back until
// the tasks sent before it had run, so the prelude reads the Project those
// left.
void ProjectOpDispatcher::handle(const RenderShot &req)
{
  if (!project::findShot(project(), req.shotId)) {
    fail(req.requestId, "shot not found");
    return;
  }
  if (!project().isSaved()) {
    fail(req.requestId, "project is not saved; save it before rendering");
    return;
  }
  if (project().activeShotId != req.shotId) {
    std::string error;
    if (!context().setActiveShot(req.shotId, &error)) {
      fail(req.requestId, error);
      return;
    }
  }

  TaskLaunch launch;
  launch.exclusive = true;
  launch.projectChanged = true; // the shot switch, and the rebind's pick
  launch.rebind = true;
  startTask(
      req.requestId,
      "render shot '" + req.shotId + "'",
      [this, shotId = req.shotId](const TaskControl &task) {
        return runTaskBody(
            [&] {
              TaskResult result;
              // Mutations are refused while the render is pending, so the
              // shot made active by the prelude is still the one; a body
              // that found otherwise would render the wrong shot.
              if (project().activeShotId != shotId) {
                result.ok = false;
                result.error = "shot '" + shotId + "' is no longer active";
                return result;
              }
              RenderShotProgress progress;
              progress.onFrame = [&](int frame, int totalFrames) {
                if (task.cancelRequested())
                  return false;
                if (m_host.shutdownRequested && m_host.shutdownRequested()) {
                  vsr::core::logStatus(
                      "[StudioServer] shot render stopped: server shutting"
                      " down");
                  return false;
                }
                task(uint64_t(frame) + 1,
                    uint64_t(totalFrames),
                    "frame " + std::to_string(frame + 1) + " of "
                        + std::to_string(totalFrames));
                return true;
              };
              RenderShotResult rendered;
              renderActiveShotToFrames(context(), &progress, &rendered);
              if (m_host.dropLatchedInputs)
                m_host.dropLatchedInputs();
              result.ok = rendered.completed;
              result.error = rendered.cancelled ? "cancelled" : rendered.error;
              result.cancelled = rendered.cancelled;
              result.message = rendered.outputDirectory.string();
              result.framesCompleted = uint64_t(rendered.framesCompleted);
              // Residency and the dirty flag were restored, the frame time
              // put back: the snapshot confirms the Project the render left.
              result.projectChanged = true;
              return result;
            },
            true);
      },
      launch);
}

void ProjectOpDispatcher::handle(const CancelTask &req)
{
  std::string error;
  if (!m_host.tasks->cancel(req.taskId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), false, false);
}

// Viewport ///////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const RequestArrayHistogram &req)
{
  auto *appContext = context().appContext();
  const auto &ref = req.array;
  vsr::scene::ArrayRef array;
  if (appContext && ref.type == ANARI_ARRAY)
    array = appContext->vsr.scene.getObject<vsr::scene::Array>(ref.objectIndex);
  if (!array) {
    fail(req.requestId,
        std::string("(") + anari::toString(ref.type) + ", "
            + std::to_string(ref.objectIndex) + ") is not an array");
    return;
  }

  ArrayHistogramResult result;
  std::string error;
  if (!computeArrayHistogram(*array, req.binCount, result, &error)) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, result);
  finish(reply, false, false);
}

} // namespace vsr::scivis_studio::server
