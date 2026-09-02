// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOpDispatcher.h"
#include "RemoteBrowse.h"
// vsr_scivis_studio_protocol
#include "ProjectSnapshot.h"
#include "StudioCodec.h"
// vsr_scivis_studio_model
#include "ProjectPersistence.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <utility>

namespace vsr::scivis_studio::server {

using namespace protocol;
using vsr::network::Message;

namespace {

// The ANARI light subtypes the monolith's Light Rig editor offers.
constexpr std::array<const char *, 5> LIGHT_SUBTYPES = {
    "directional", "point", "quad", "spot", "ring"};

template <typename T>
std::optional<ProjectRequest> decodeAs(const Message &msg)
{
  auto payload = decode<T>(msg);
  if (!payload)
    return {};
  return ProjectRequest{std::move(*payload)};
}

template <typename T>
constexpr bool isOneOf(const ProjectRequest &request)
{
  return std::holds_alternative<T>(request);
}

// The failure of a task body whose dataset is gone by the time it runs.
TaskResult datasetNotFound()
{
  TaskResult result;
  result.ok = false;
  result.error = "dataset not found";
  return result;
}

// The UI-state pieces saveProject() takes, read from an opaque tree.
struct UIStateParts
{
  vsr::core::DataNode *windows{nullptr};
  std::string layout;
  vsr::core::DataNode *settings{nullptr};
};

UIStateParts uiStateParts(const SubtreePtr &tree)
{
  UIStateParts parts;
  if (!tree)
    return parts;
  auto &root = tree->root();
  parts.windows = root.child("windows");
  parts.settings = root.child("settings");
  if (auto *layout = root.child("layout"))
    parts.layout = layout->getValueOr<std::string>("");
  return parts;
}

} // namespace

// Request plumbing ///////////////////////////////////////////////////////////

bool isProjectRequestType(StudioMessageType type)
{
  const auto value = int(type);
  return (value >= int(StudioMessageType::NewProject)
             && value <= int(StudioMessageType::ListDirectory))
      || type == StudioMessageType::CancelTask;
}

std::optional<ProjectRequest> decodeProjectRequest(const Message &msg)
{
  const auto type = messageType(msg);
  if (!type)
    return {};
  switch (*type) {
  case StudioMessageType::NewProject:
    return decodeAs<NewProject>(msg);
  case StudioMessageType::OpenProject:
    return decodeAs<OpenProject>(msg);
  case StudioMessageType::SaveProject:
    return decodeAs<SaveProject>(msg);
  case StudioMessageType::ImportStaticDataset:
    return decodeAs<ImportStaticDataset>(msg);
  case StudioMessageType::ImportFileAnimationDataset:
    return decodeAs<ImportFileAnimationDataset>(msg);
  case StudioMessageType::DeclareFileAnimationDataset:
    return decodeAs<DeclareFileAnimationDataset>(msg);
  case StudioMessageType::ReimportDataset:
    return decodeAs<ReimportDataset>(msg);
  case StudioMessageType::RenameDataset:
    return decodeAs<RenameDataset>(msg);
  case StudioMessageType::RemoveDataset:
    return decodeAs<RemoveDataset>(msg);
  case StudioMessageType::LoadDataset:
    return decodeAs<LoadDataset>(msg);
  case StudioMessageType::UnloadDataset:
    return decodeAs<UnloadDataset>(msg);
  case StudioMessageType::RefreshDatasetAvailability:
    return decodeAs<RefreshDatasetAvailability>(msg);
  case StudioMessageType::SaveDatasetArchive:
    return decodeAs<SaveDatasetArchive>(msg);
  case StudioMessageType::LoadDatasetArchive:
    return decodeAs<LoadDatasetArchive>(msg);
  case StudioMessageType::DiscoverDatasetCandidates:
    return decodeAs<DiscoverDatasetCandidates>(msg);
  case StudioMessageType::IncorporateDatasetCandidate:
    return decodeAs<IncorporateDatasetCandidate>(msg);
  case StudioMessageType::CreateShot:
    return decodeAs<CreateShot>(msg);
  case StudioMessageType::RemoveShot:
    return decodeAs<RemoveShot>(msg);
  case StudioMessageType::UpdateShot:
    return decodeAs<UpdateShot>(msg);
  case StudioMessageType::SetActiveShot:
    return decodeAs<SetActiveShot>(msg);
  case StudioMessageType::CreateLightRig:
    return decodeAs<CreateLightRig>(msg);
  case StudioMessageType::CloneLightRig:
    return decodeAs<CloneLightRig>(msg);
  case StudioMessageType::RemoveLightRig:
    return decodeAs<RemoveLightRig>(msg);
  case StudioMessageType::RenameLightRig:
    return decodeAs<RenameLightRig>(msg);
  case StudioMessageType::AddLightToRig:
    return decodeAs<AddLightToRig>(msg);
  case StudioMessageType::RemoveLightFromRig:
    return decodeAs<RemoveLightFromRig>(msg);
  case StudioMessageType::CreateCameraRig:
    return decodeAs<CreateCameraRig>(msg);
  case StudioMessageType::RemoveCameraRig:
    return decodeAs<RemoveCameraRig>(msg);
  case StudioMessageType::RenameCameraRig:
    return decodeAs<RenameCameraRig>(msg);
  case StudioMessageType::SaveCameraRigArchive:
    return decodeAs<SaveCameraRigArchive>(msg);
  case StudioMessageType::LoadCameraRigArchive:
    return decodeAs<LoadCameraRigArchive>(msg);
  case StudioMessageType::SaveLightRigArchive:
    return decodeAs<SaveLightRigArchive>(msg);
  case StudioMessageType::LoadLightRigArchive:
    return decodeAs<LoadLightRigArchive>(msg);
  case StudioMessageType::CreateColorMap:
    return decodeAs<CreateColorMap>(msg);
  case StudioMessageType::RenameColorMap:
    return decodeAs<RenameColorMap>(msg);
  case StudioMessageType::RemoveColorMap:
    return decodeAs<RemoveColorMap>(msg);
  case StudioMessageType::ListRoots:
    return decodeAs<ListRoots>(msg);
  case StudioMessageType::ListDirectory:
    return decodeAs<ListDirectory>(msg);
  case StudioMessageType::CancelTask:
    return decodeAs<CancelTask>(msg);
  default:
    return {};
  }
}

bool waitsForQueuedTasks(const ProjectRequest &request)
{
  const bool bypasses = isOneOf<OpenProject>(request)
      || isOneOf<SaveProject>(request) || isOneOf<ImportStaticDataset>(request)
      || isOneOf<ImportFileAnimationDataset>(request)
      || isOneOf<ReimportDataset>(request) || isOneOf<LoadDataset>(request)
      || isOneOf<SaveDatasetArchive>(request)
      || isOneOf<LoadDatasetArchive>(request)
      || isOneOf<IncorporateDatasetCandidate>(request)
      || isOneOf<ListRoots>(request) || isOneOf<ListDirectory>(request)
      || isOneOf<CancelTask>(request);
  return !bypasses;
}

// Dispatcher /////////////////////////////////////////////////////////////////

ProjectOpDispatcher::ProjectOpDispatcher(Host host) : m_host(std::move(host)) {}

void ProjectOpDispatcher::dispatch(const ProjectRequest &request)
{
  std::visit([this](const auto &r) { handle(r); }, request);
}

void ProjectOpDispatcher::runOneTask()
{
  if (auto ran = m_host.tasks->runOne()) {
    if (ran->result.projectChanged)
      sendSnapshot();
  }
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

void ProjectOpDispatcher::fail(uint64_t requestId, const std::string &error)
{
  vsr::core::logWarning("[StudioServer] request %llu refused: %s",
      static_cast<unsigned long long>(requestId),
      error.c_str());
  finish(makeErrorReply(requestId, error), false, false);
}

void ProjectOpDispatcher::startTask(
    uint64_t requestId, std::string description, TaskBody body)
{
  const auto taskId =
      m_host.tasks->enqueue(std::move(description), std::move(body));
  auto reply = makeOkReply(requestId);
  setResults(reply, TaskStartedResult{taskId});
  finish(reply, false, false);
}

TaskResult ProjectOpDispatcher::runTaskBody(
    const std::function<TaskResult()> &body, bool rebind)
{
  auto result = body();
  if (rebind && result.ok)
    m_host.rebindActiveShot();
  m_host.flushScenePushes();
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
      [this, directory = *directory](const TaskProgressFunction &progress) {
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
              // The same {windows, layout, settings} shape the manifest holds,
              // handed back to the client at bootstrap.
              auto ui = makeSubtree();
              std::string layout;
              if (!context().openStagedProject(stage,
                      &ui->root()["windows"],
                      &layout,
                      &ui->root()["settings"],
                      &result.error)) {
                result.ok = false;
                return result;
              }
              ui->root()["layout"] = layout;
              *m_host.uiState = ui;
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
      [this, named, uiState = req.uiState](
          const TaskProgressFunction &progress) {
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
              const auto parts = uiStateParts(tree);
              if (!context().saveProject(*directory,
                      parts.windows,
                      parts.layout,
                      parts.settings,
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
      [this, req, source = *source](const TaskProgressFunction &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              progress("importing");
              auto *dataset = req.fromSubtreeArchive
                  ? context().addStaticDatasetFromSubtree(req.name, source)
                  : context().addStaticDataset(
                        req.name, source, req.importerType);
              if (!dataset) {
                result.ok = false;
                result.error = "import failed";
                return result;
              }
              // A failed import still leaves its record in the project.
              result.projectChanged = true;
              result.message = dataset->id;
              if (dataset->status != DatasetStatus::Available) {
                result.ok = false;
                result.error = "dataset '" + dataset->name + "' import failed ("
                    + dataset::displayStatus(*dataset)
                    + "); see the server log";
              }
              return result;
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
      [this, req, sources = std::move(sources)](
          const TaskProgressFunction &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              progress("importing");
              FileAnimationDatasetOptions options;
              options.setActiveShotFrameCount = req.setActiveShotFrameCount;
              auto *dataset = context().addFileAnimationDataset(
                  req.name, sources, req.importerType, options);
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
                    + dataset::displayStatus(*dataset)
                    + "); see the server log";
              }
              return result;
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
      [this, id = req.datasetId](const TaskProgressFunction &progress) {
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
      [this, id = req.datasetId](const TaskProgressFunction &progress) {
        return runTaskBody(
            [&] {
              TaskResult result;
              const auto *before = project::findDataset(project(), id);
              if (!before)
                return datasetNotFound();
              progress("loading");
              const auto statusBefore = before->status;
              result.ok = context().loadDataset(id, &result.error);
              // A failed load marks the dataset Unavailable: still a change.
              const auto *after = project::findDataset(project(), id);
              result.projectChanged =
                  result.ok || (after && after->status != statusBefore);
              return result;
            },
            false);
      });
}

void ProjectOpDispatcher::handle(const UnloadDataset &req)
{
  const auto *before = project::findDataset(project(), req.datasetId);
  const auto statusBefore =
      before ? before->status : DatasetStatus::Unavailable;
  std::string error;
  const bool ok = context().unloadDataset(req.datasetId, &error);
  const auto *after = project::findDataset(project(), req.datasetId);
  const bool changed = ok || (after && after->status != statusBefore);
  if (!ok) {
    vsr::core::logWarning("[StudioServer] request %llu refused: %s",
        static_cast<unsigned long long>(req.requestId),
        error.c_str());
    finish(makeErrorReply(req.requestId, error), changed, false);
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
      [this, id = req.datasetId, file = *file](
          const TaskProgressFunction &progress) {
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
      [this, file = *file](const TaskProgressFunction &progress) {
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
      [this, candidate, name = req.name](const TaskProgressFunction &progress) {
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
  const auto subtype =
      req.subtype.empty() ? std::string("directional") : req.subtype;
  if (std::none_of(LIGHT_SUBTYPES.begin(),
          LIGHT_SUBTYPES.end(),
          [&](const char *known) { return subtype == known; })) {
    fail(req.requestId, "unknown light subtype '" + subtype + "'");
    return;
  }
  auto node = context().addLightToRig(*rig, subtype);
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

void ProjectOpDispatcher::handle(const CancelTask &req)
{
  std::string error;
  if (!m_host.tasks->cancel(req.taskId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), false, false);
}

} // namespace vsr::scivis_studio::server
