// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOps.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <utility>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

std::string quoted(const std::string &text)
{
  return "'" + text + "'";
}

std::string quoted(const std::filesystem::path &path)
{
  return quoted(path.generic_string());
}

} // namespace

const char *toString(TaskState state)
{
  switch (state) {
  case TaskState::Queued:
    return "Queued";
  case TaskState::Running:
    return "Running";
  case TaskState::Completed:
    return "Completed";
  case TaskState::Failed:
    return "Failed";
  }
  return "Unknown";
}

// Construction ///////////////////////////////////////////////////////////////

ProjectOps::ProjectOps(Sender sender) : m_sender(std::move(sender)) {}

// Generic sends //////////////////////////////////////////////////////////////

RequestHandle ProjectOps::submit(uint64_t requestId,
    vsr::network::Message &&msg,
    ReplyCallback callback,
    std::string taskLabel)
{
  RequestHandle handle;
  handle.requestId = requestId;
  const bool sent = m_sender && m_sender(std::move(msg));
  if (!sent) {
    if (callback) {
      m_undeliverable.push_back(makeErrorReply(requestId, "not connected"));
      m_pending.push_back(Pending{requestId, std::move(callback), {}});
    }
    return handle;
  }
  m_pending.push_back(
      Pending{requestId, std::move(callback), std::move(taskLabel)});
  return handle;
}

ProjectOps::Pending *ProjectOps::findPending(uint64_t requestId)
{
  auto it = std::find_if(m_pending.begin(),
      m_pending.end(),
      [&](const auto &p) { return p.requestId == requestId; });
  return it == m_pending.end() ? nullptr : &*it;
}

const ProjectOps::Pending *ProjectOps::findPending(uint64_t requestId) const
{
  auto it = std::find_if(m_pending.begin(),
      m_pending.end(),
      [&](const auto &p) { return p.requestId == requestId; });
  return it == m_pending.end() ? nullptr : &*it;
}

size_t ProjectOps::pendingCount() const
{
  return m_pending.size();
}

bool ProjectOps::pending(RequestHandle handle) const
{
  return findPending(handle.requestId) != nullptr;
}

void ProjectOps::forget(RequestHandle handle)
{
  if (auto *entry = findPending(handle.requestId))
    entry->callback = nullptr;
}

// Project ////////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::newProject(ReplyCallback callback)
{
  return send(NewProject{}, std::move(callback));
}

RequestHandle ProjectOps::openProject(const std::filesystem::path &directory,
    ResultCallback<TaskStartedResult> callback)
{
  OpenProject req;
  req.directory = directory;
  return sendForResult(
      std::move(req), std::move(callback), "Open project " + quoted(directory));
}

RequestHandle ProjectOps::saveProject(
    const std::optional<std::filesystem::path> &directory,
    SubtreePtr uiState,
    ResultCallback<TaskStartedResult> callback)
{
  SaveProject req;
  req.directory = directory;
  req.uiState = std::move(uiState);
  return sendForResult(std::move(req),
      std::move(callback),
      directory ? "Save project as " + quoted(*directory) : "Save project");
}

// Datasets ///////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::importStaticDataset(const std::string &name,
    const std::filesystem::path &sourcePath,
    vsr::io::ImporterType importerType,
    bool fromSubtreeArchive,
    ResultCallback<TaskStartedResult> callback)
{
  ImportStaticDataset req;
  req.name = name;
  req.sourcePath = sourcePath;
  req.importerType = importerType;
  req.fromSubtreeArchive = fromSubtreeArchive;
  return sendForResult(
      std::move(req), std::move(callback), "Import " + quoted(sourcePath));
}

RequestHandle ProjectOps::importFileAnimationDataset(const std::string &name,
    const std::vector<std::filesystem::path> &sourcePaths,
    vsr::io::ImporterType importerType,
    bool setActiveShotFrameCount,
    ResultCallback<TaskStartedResult> callback)
{
  ImportFileAnimationDataset req;
  req.name = name;
  req.sourcePaths = sourcePaths;
  req.importerType = importerType;
  req.setActiveShotFrameCount = setActiveShotFrameCount;
  return sendForResult(std::move(req),
      std::move(callback),
      "Import file animation " + quoted(name) + " ("
          + std::to_string(sourcePaths.size()) + " files)");
}

RequestHandle ProjectOps::declareFileAnimationDataset(const std::string &name,
    const std::vector<std::string> &sourceList,
    vsr::io::ImporterType importerType,
    bool setActiveShotFrameCount,
    ResultCallback<DatasetCreatedResult> callback)
{
  DeclareFileAnimationDataset req;
  req.name = name;
  req.sourceList = sourceList;
  req.importerType = importerType;
  req.setActiveShotFrameCount = setActiveShotFrameCount;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::reimportDataset(
    const DatasetID &datasetId, ResultCallback<TaskStartedResult> callback)
{
  ReimportDataset req;
  req.datasetId = datasetId;
  return sendForResult(
      std::move(req), std::move(callback), "Reimport dataset " + datasetId);
}

RequestHandle ProjectOps::renameDataset(const DatasetID &datasetId,
    const std::string &newName,
    ReplyCallback callback)
{
  RenameDataset req;
  req.datasetId = datasetId;
  req.newName = newName;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::removeDataset(
    const DatasetID &datasetId, bool keepAssetFile, ReplyCallback callback)
{
  RemoveDataset req;
  req.datasetId = datasetId;
  req.keepAssetFile = keepAssetFile;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::loadDataset(
    const DatasetID &datasetId, ResultCallback<TaskStartedResult> callback)
{
  LoadDataset req;
  req.datasetId = datasetId;
  return sendForResult(
      std::move(req), std::move(callback), "Load dataset " + datasetId);
}

RequestHandle ProjectOps::unloadDataset(
    const DatasetID &datasetId, ReplyCallback callback)
{
  UnloadDataset req;
  req.datasetId = datasetId;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::refreshDatasetAvailability(
    const DatasetID &datasetId, ReplyCallback callback)
{
  RefreshDatasetAvailability req;
  req.datasetId = datasetId;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::saveDatasetArchive(const DatasetID &datasetId,
    const std::filesystem::path &file,
    ResultCallback<TaskStartedResult> callback)
{
  SaveDatasetArchive req;
  req.datasetId = datasetId;
  req.file = file;
  return sendForResult(std::move(req),
      std::move(callback),
      "Save dataset archive " + quoted(file));
}

RequestHandle ProjectOps::loadDatasetArchive(const std::filesystem::path &file,
    ResultCallback<TaskStartedResult> callback)
{
  LoadDatasetArchive req;
  req.file = file;
  return sendForResult(std::move(req),
      std::move(callback),
      "Load dataset archive " + quoted(file));
}

RequestHandle ProjectOps::discoverDatasetCandidates(
    ResultCallback<DiscoverDatasetCandidatesResult> callback)
{
  return sendForResult(DiscoverDatasetCandidates{}, std::move(callback));
}

RequestHandle ProjectOps::incorporateDatasetCandidate(
    const std::filesystem::path &file,
    const std::string &proposedName,
    const std::string &name,
    ResultCallback<TaskStartedResult> callback)
{
  IncorporateDatasetCandidate req;
  req.file = file;
  req.proposedName = proposedName;
  req.name = name;
  return sendForResult(std::move(req),
      std::move(callback),
      "Incorporate dataset " + quoted(name.empty() ? proposedName : name));
}

// Shots //////////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::createShot(
    const std::string &name, ResultCallback<ShotCreatedResult> callback)
{
  CreateShot req;
  req.name = name;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::removeShot(
    const ShotID &shotId, ReplyCallback callback)
{
  RemoveShot req;
  req.shotId = shotId;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::updateShot(const Shot &shot, ReplyCallback callback)
{
  UpdateShot req;
  req.shot = shot;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::setActiveShot(
    const ShotID &shotId, ReplyCallback callback)
{
  SetActiveShot req;
  req.shotId = shotId;
  return send(std::move(req), std::move(callback));
}

// Light rigs /////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::createLightRig(
    const std::string &name, ResultCallback<LightRigCreatedResult> callback)
{
  CreateLightRig req;
  req.name = name;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::cloneLightRig(const LightRigID &lightRigId,
    ResultCallback<LightRigCreatedResult> callback)
{
  CloneLightRig req;
  req.lightRigId = lightRigId;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::removeLightRig(
    const LightRigID &lightRigId, ReplyCallback callback)
{
  RemoveLightRig req;
  req.lightRigId = lightRigId;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::renameLightRig(const LightRigID &lightRigId,
    const std::string &newName,
    ReplyCallback callback)
{
  RenameLightRig req;
  req.lightRigId = lightRigId;
  req.newName = newName;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::addLightToRig(const LightRigID &lightRigId,
    const std::string &subtype,
    ResultCallback<LightAddedResult> callback)
{
  AddLightToRig req;
  req.lightRigId = lightRigId;
  req.subtype = subtype;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::removeLightFromRig(const LightRigID &lightRigId,
    const SceneNodeRef &lightNode,
    ReplyCallback callback)
{
  RemoveLightFromRig req;
  req.lightRigId = lightRigId;
  req.lightNode = lightNode;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::saveLightRigArchive(const LightRigID &lightRigId,
    const std::filesystem::path &file,
    ReplyCallback callback)
{
  SaveLightRigArchive req;
  req.lightRigId = lightRigId;
  req.file = file;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::loadLightRigArchive(const std::filesystem::path &file,
    ResultCallback<LightRigCreatedResult> callback)
{
  LoadLightRigArchive req;
  req.file = file;
  return sendForResult(std::move(req), std::move(callback));
}

// Camera rigs ////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::createCameraRig(
    const std::string &name, ResultCallback<CameraRigCreatedResult> callback)
{
  CreateCameraRig req;
  req.name = name;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::removeCameraRig(
    const CameraRigID &cameraRigId, ReplyCallback callback)
{
  RemoveCameraRig req;
  req.cameraRigId = cameraRigId;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::renameCameraRig(const CameraRigID &cameraRigId,
    const std::string &newName,
    ReplyCallback callback)
{
  RenameCameraRig req;
  req.cameraRigId = cameraRigId;
  req.newName = newName;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::saveCameraRigArchive(const CameraRigID &cameraRigId,
    const std::filesystem::path &file,
    ReplyCallback callback)
{
  SaveCameraRigArchive req;
  req.cameraRigId = cameraRigId;
  req.file = file;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::loadCameraRigArchive(
    const std::filesystem::path &file,
    ResultCallback<CameraRigCreatedResult> callback)
{
  LoadCameraRigArchive req;
  req.file = file;
  return sendForResult(std::move(req), std::move(callback));
}

// Color maps /////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::createColorMap(
    const std::string &name, ResultCallback<ColorMapCreatedResult> callback)
{
  CreateColorMap req;
  req.name = name;
  return sendForResult(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::renameColorMap(const ColorMapID &colorMapId,
    const std::string &newName,
    ReplyCallback callback)
{
  RenameColorMap req;
  req.colorMapId = colorMapId;
  req.newName = newName;
  return send(std::move(req), std::move(callback));
}

RequestHandle ProjectOps::removeColorMap(
    const ColorMapID &colorMapId, ReplyCallback callback)
{
  RemoveColorMap req;
  req.colorMapId = colorMapId;
  return send(std::move(req), std::move(callback));
}

// Remote Browse //////////////////////////////////////////////////////////////

RequestHandle ProjectOps::listRoots(ResultCallback<ListRootsResult> callback)
{
  return sendForResult(ListRoots{}, std::move(callback));
}

RequestHandle ProjectOps::listDirectory(const std::filesystem::path &directory,
    ResultCallback<ListDirectoryResult> callback)
{
  ListDirectory req;
  req.directory = directory;
  return sendForResult(std::move(req), std::move(callback));
}

// Server Tasks ///////////////////////////////////////////////////////////////

RequestHandle ProjectOps::cancelTask(uint64_t taskId, ReplyCallback callback)
{
  CancelTask req;
  req.taskId = taskId;
  return send(std::move(req), std::move(callback));
}

const std::vector<TaskRecord> &ProjectOps::tasks() const
{
  return m_tasks;
}

const TaskRecord *ProjectOps::task(uint64_t taskId) const
{
  auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const auto &t) {
    return t.taskId == taskId;
  });
  return it == m_tasks.end() ? nullptr : &*it;
}

bool ProjectOps::tasksActive() const
{
  return std::any_of(m_tasks.begin(), m_tasks.end(), [](const auto &t) {
    return !t.finished();
  });
}

void ProjectOps::clearFinishedTasks()
{
  m_tasks.erase(std::remove_if(m_tasks.begin(),
                    m_tasks.end(),
                    [](const auto &t) { return t.finished(); }),
      m_tasks.end());
}

TaskRecord &ProjectOps::recordFor(uint64_t taskId)
{
  auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const auto &t) {
    return t.taskId == taskId;
  });
  if (it != m_tasks.end())
    return *it;
  TaskRecord record;
  record.taskId = taskId;
  record.label = "Task " + std::to_string(taskId);
  m_tasks.push_back(std::move(record));
  return m_tasks.back();
}

// Driven by ServerConnection /////////////////////////////////////////////////

void ProjectOps::handleReply(const ProjectOpReply &reply)
{
  // Take the entry out before running anything: the callback may send again.
  Pending entry;
  if (auto *found = findPending(reply.requestId)) {
    entry = std::move(*found);
    m_pending.erase(m_pending.begin() + (found - m_pending.data()));
  } else {
    vsr::core::logWarning("[ProjectOps] reply to unknown request %llu (%s)",
        static_cast<unsigned long long>(reply.requestId),
        reply.ok ? "ok" : reply.error.c_str());
  }

  if (reply.ok) {
    if (auto started = results<TaskStartedResult>(reply)) {
      TaskRecord &record = recordFor(started->taskId);
      if (!entry.taskLabel.empty())
        record.label = entry.taskLabel;
    }
  }

  if (entry.callback)
    entry.callback(reply);
}

void ProjectOps::handleTaskProgress(const TaskProgress &progress)
{
  TaskRecord &record = recordFor(progress.taskId);
  if (!record.finished())
    record.state = TaskState::Running;
  record.lastProgress.current = progress.current;
  record.lastProgress.total = progress.total;
  record.lastProgress.message = progress.message;
}

void ProjectOps::handleTaskCompleted(const TaskCompleted &completed)
{
  TaskRecord &record = recordFor(completed.taskId);
  record.state = TaskState::Completed;
  if (!completed.message.empty())
    record.lastProgress.message = completed.message;
  record.error.clear();
}

void ProjectOps::handleTaskFailed(const TaskFailed &failed)
{
  TaskRecord &record = recordFor(failed.taskId);
  record.state = TaskState::Failed;
  record.error = failed.error;
}

void ProjectOps::failAllPending(const std::string &error)
{
  // Swap the map out first so a callback that sends anew is not swept up.
  std::vector<Pending> pending = std::move(m_pending);
  m_pending.clear();
  m_undeliverable.clear();
  for (auto &entry : pending) {
    if (entry.callback)
      entry.callback(makeErrorReply(entry.requestId, error));
  }
}

void ProjectOps::clearTasks()
{
  m_tasks.clear();
}

void ProjectOps::poll()
{
  if (m_undeliverable.empty())
    return;
  std::vector<ProjectOpReply> replies = std::move(m_undeliverable);
  m_undeliverable.clear();
  for (const auto &reply : replies)
    handleReply(reply);
}

} // namespace vsr::scivis_studio::client
