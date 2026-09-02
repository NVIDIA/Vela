// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOps.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <cctype>
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

// `word` occurs in `text` bounded by non-identifier characters, so that
// "LoadDataset" is not found inside "LoadDatasetArchive".
bool containsWord(const std::string &text, const std::string &word)
{
  const auto isIdent = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  };
  for (auto pos = text.find(word); pos != std::string::npos;
       pos = text.find(word, pos + 1)) {
    const bool startsWord = pos == 0 || !isIdent(text[pos - 1]);
    const auto end = pos + word.size();
    const bool endsWord = end == text.size() || !isIdent(text[end]);
    if (startsWord && endsWord)
      return true;
  }
  return false;
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

bool ProjectOps::Pending::hasCallback() const
{
  return std::visit([](const auto &cb) { return bool(cb); }, callback);
}

bool ProjectOps::Pending::isPick() const
{
  return type == StudioMessageType::Pick;
}

RequestHandle ProjectOps::submit(uint64_t requestId,
    StudioMessageType type,
    vsr::network::Message &&msg,
    Callback callback,
    std::string taskLabel)
{
  RequestHandle handle;
  handle.requestId = requestId;
  Pending entry{requestId, type, std::move(callback), {}};
  const bool sent = m_sender && m_sender(std::move(msg));
  if (!sent) {
    if (entry.hasCallback()) {
      m_undeliverable.push_back(makeErrorReply(requestId, "not connected"));
      m_pending.push_back(std::move(entry));
    }
    return handle;
  }
  entry.taskLabel = std::move(taskLabel);
  m_pending.push_back(std::move(entry));
  return handle;
}

void ProjectOps::fail(Pending &entry, const ProjectOpReply &reply)
{
  if (auto *cb = std::get_if<ReplyCallback>(&entry.callback)) {
    if (*cb)
      (*cb)(reply);
  } else if (auto *pick = std::get_if<PickCallback>(&entry.callback)) {
    if (*pick)
      (*pick)(std::nullopt);
  }
}

std::optional<ProjectOps::Pending> ProjectOps::takePending(uint64_t requestId)
{
  auto *found = findPending(requestId);
  if (!found)
    return {};
  Pending entry = std::move(*found);
  m_pending.erase(m_pending.begin() + (found - m_pending.data()));
  return entry;
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
  auto *entry = findPending(handle.requestId);
  if (!entry)
    return;
  if (!entry->isPick()) {
    entry->callback = ReplyCallback{};
    return;
  }
  // A superseded pick is never answered (latest-wins on the server); a late
  // reply for it just logs as unknown.
  takePending(handle.requestId);
  m_undeliverable.erase(std::remove_if(m_undeliverable.begin(),
                            m_undeliverable.end(),
                            [&](const auto &reply) {
                              return reply.requestId == handle.requestId;
                            }),
      m_undeliverable.end());
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

// Playback ///////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::setPlaying(
    const ShotID &shotId, bool playing, ReplyCallback callback)
{
  SetPlaying req;
  req.shotId = shotId;
  req.playing = playing;
  return send(std::move(req), std::move(callback));
}

// Offline render /////////////////////////////////////////////////////////////

RequestHandle ProjectOps::renderShot(
    const ShotID &shotId, ResultCallback<TaskStartedResult> callback)
{
  RenderShot req;
  req.shotId = shotId;
  return sendForResult(
      std::move(req), std::move(callback), "Render shot " + quoted(shotId));
}

// Array histogram ////////////////////////////////////////////////////////////

RequestHandle ProjectOps::requestArrayHistogram(const SceneObjectRef &array,
    uint32_t binCount,
    ResultCallback<ArrayHistogramResult> callback)
{
  RequestArrayHistogram req;
  req.array = array;
  req.binCount = binCount;
  return sendForResult(std::move(req), std::move(callback));
}

// Pick ///////////////////////////////////////////////////////////////////////

RequestHandle ProjectOps::pick(int x, int y, PickCallback callback)
{
  Pick req;
  req.requestId = m_nextRequestId++;
  req.x = x;
  req.y = y;
  return submit(
      req.requestId, Pick::MESSAGE_TYPE, encode(req), std::move(callback), {});
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

bool ProjectOps::renderActive() const
{
  return std::any_of(m_tasks.begin(), m_tasks.end(), [](const auto &t) {
    return t.render && !t.finished();
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
  if (it != m_tasks.end()) {
    if (it->stale) {
      // The server speaks of this id again: the client-side failure was
      // provisional. Keep the label, start the state over.
      it->stale = false;
      it->state = TaskState::Queued;
      it->lastProgress = {};
      it->outcome.clear();
      it->error.clear();
      it->framesCompleted = 0;
    }
    return *it;
  }
  TaskRecord record;
  record.taskId = taskId;
  record.label = "Task " + std::to_string(taskId);
  m_tasks.push_back(std::move(record));
  return m_tasks.back();
}

TaskRecord &ProjectOps::freshRecordFor(uint64_t taskId)
{
  TaskRecord &record = recordFor(taskId);
  record = TaskRecord{};
  record.taskId = taskId;
  record.label = "Task " + std::to_string(taskId);
  return record;
}

// Driven by ServerConnection /////////////////////////////////////////////////

void ProjectOps::handleReply(const ProjectOpReply &reply)
{
  // Take the entry out before running anything: the callback may send again.
  Pending entry;
  if (auto taken = takePending(reply.requestId)) {
    entry = std::move(*taken);
  } else {
    vsr::core::logWarning("[ProjectOps] reply to unknown request %llu (%s)",
        static_cast<unsigned long long>(reply.requestId),
        reply.ok ? "ok" : reply.error.c_str());
  }

  if (!reply.ok) {
    // For a pick this can only be its failure (undeliverable or refused):
    // it fails with an absent reply.
    fail(entry, reply);
    return;
  }
  if (auto started = results<TaskStartedResult>(reply)) {
    // A TaskStarted names a new task whatever record its id has: a stale one
    // the replay never revived, or a finished one of a server process since
    // restarted (ids count from 1 again).
    TaskRecord &record = freshRecordFor(started->taskId);
    if (!entry.taskLabel.empty())
      record.label = entry.taskLabel;
    record.render = entry.type == StudioMessageType::RenderShot;
  }
  if (auto *cb = std::get_if<ReplyCallback>(&entry.callback); cb && *cb)
    (*cb)(reply);
}

void ProjectOps::handlePickReply(const PickReply &reply)
{
  // Out before running: the callback may pick again.
  auto entry = takePending(reply.requestId);
  if (!entry || !entry->isPick()) {
    vsr::core::logWarning("[ProjectOps] PickReply to unknown request %llu",
        static_cast<unsigned long long>(reply.requestId));
    return;
  }
  if (auto *cb = std::get_if<PickCallback>(&entry->callback); cb && *cb)
    (*cb)(reply);
}

void ProjectOps::handleTaskProgress(const TaskProgress &progress)
{
  // A task ends once and the replay repeats endings, not progress, so
  // progress for a record that finished (and was not failed by this client)
  // is a new task of a restarted server under a reused id: it starts over,
  // named like one never heard of.
  const TaskRecord *existing = task(progress.taskId);
  const bool fresh =
      !existing || (existing->finished() && !existing->stale);
  TaskRecord &record =
      fresh ? freshRecordFor(progress.taskId) : recordFor(progress.taskId);
  if (fresh && !progress.message.empty())
    record.label = progress.message; // the replay's description
  record.state = TaskState::Running;
  record.lastProgress.current = progress.current;
  record.lastProgress.total = progress.total;
  record.lastProgress.message = progress.message;
}

void ProjectOps::handleTaskCompleted(const TaskCompleted &completed)
{
  TaskRecord &record = recordFor(completed.taskId);
  record.state = TaskState::Completed;
  // The last phase text stays for the panel row; the outcome, when the task
  // has one, replaces it there and is what the completion toast quotes.
  record.outcome = completed.message;
  if (!completed.message.empty())
    record.lastProgress.message = completed.message;
  record.framesCompleted = completed.framesCompleted;
  record.error.clear();
}

void ProjectOps::handleTaskFailed(const TaskFailed &failed)
{
  TaskRecord &record = recordFor(failed.taskId);
  record.state = TaskState::Failed;
  record.error = failed.error;
  record.framesCompleted = failed.framesCompleted; // a cancelled render's
}

bool ProjectOps::failOldestNamed(const std::string &message)
{
  auto it = std::find_if(m_pending.begin(),
      m_pending.end(),
      [&](const auto &p) { return containsWord(message, toString(p.type)); });
  if (it == m_pending.end())
    return false;
  const auto requestId = it->requestId;
  vsr::core::logWarning("[ProjectOps] request %llu (%s) refused: %s",
      static_cast<unsigned long long>(requestId),
      toString(it->type),
      message.c_str());
  if (auto entry = takePending(requestId))
    fail(*entry, makeErrorReply(requestId, message));
  return true;
}

void ProjectOps::failAllPending(const std::string &error)
{
  // Swap the list out first so a callback that sends anew is not swept up.
  std::vector<Pending> pending = std::move(m_pending);
  m_pending.clear();
  m_undeliverable.clear();
  for (auto &entry : pending)
    fail(entry, makeErrorReply(entry.requestId, error));
}

void ProjectOps::failUnfinishedTasks(const std::string &error)
{
  for (auto &record : m_tasks) {
    if (record.finished())
      continue;
    record.state = TaskState::Failed;
    record.error = error;
    record.stale = true;
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
