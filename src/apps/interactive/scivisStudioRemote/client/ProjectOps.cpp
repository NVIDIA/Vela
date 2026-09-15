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

std::string TaskRecord::describeEnding() const
{
  if (!finished())
    return {};
  std::string text = label.empty() ? "<task>" : label;
  if (state == TaskState::Completed) {
    text += " completed";
    if (framesCompleted != 0)
      text += " (" + std::to_string(framesCompleted) + " frames)";
    if (!outcome.empty())
      text += ": " + outcome;
  } else {
    text += " failed";
    if (framesCompleted != 0)
      text += " after " + std::to_string(framesCompleted) + " frames";
    text += ": " + error;
  }
  return text;
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

// Task labels ////////////////////////////////////////////////////////////////

std::string taskLabel(const OpenProject &req)
{
  return "Open project " + quoted(req.directory);
}

std::string taskLabel(const SaveProject &req)
{
  return req.directory ? "Save project as " + quoted(*req.directory)
                       : "Save project";
}

std::string taskLabel(const ImportStaticDataset &req)
{
  return "Import " + quoted(req.sourcePath);
}

std::string taskLabel(const ImportSubtreeDataset &req)
{
  return "Import " + quoted(req.sourcePath);
}

std::string taskLabel(const ImportFileAnimationDataset &req)
{
  return "Import file animation " + quoted(req.name) + " ("
      + std::to_string(req.sourcePaths.size()) + " files)";
}

std::string taskLabel(const ReimportDataset &req)
{
  return "Reimport dataset " + req.datasetId;
}

std::string taskLabel(const LoadDataset &req)
{
  return "Load dataset " + req.datasetId;
}

std::string taskLabel(const SaveDatasetArchive &req)
{
  return "Save dataset archive " + quoted(req.file);
}

std::string taskLabel(const LoadDatasetArchive &req)
{
  return "Load dataset archive " + quoted(req.file);
}

std::string taskLabel(const IncorporateDatasetCandidate &req)
{
  return "Incorporate dataset "
      + quoted(req.name.empty() ? req.proposedName : req.name);
}

std::string taskLabel(const RenderShot &req)
{
  return "Render shot " + quoted(req.shotId);
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

TaskRecord *ProjectOps::findRecord(uint64_t taskId)
{
  auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const auto &t) {
    return t.taskId == taskId;
  });
  return it == m_tasks.end() ? nullptr : &*it;
}

TaskRecord &ProjectOps::recordFor(uint64_t taskId)
{
  if (TaskRecord *record = findRecord(taskId))
    return *record;
  return startOver(taskId);
}

TaskRecord &ProjectOps::startOver(uint64_t taskId)
{
  TaskRecord fresh;
  fresh.taskId = taskId;
  fresh.label = "Task " + std::to_string(taskId);
  TaskRecord *existing = findRecord(taskId);
  if (!existing) {
    m_tasks.push_back(std::move(fresh));
    return m_tasks.back();
  }
  // A record this client failed at BootstrapBegin is one the server never
  // finished, so a render it named may still be running and still refusing
  // edits: keep the flag so the editors go on saying so. The label is not
  // kept -- see startOver's comment in the header -- and a TaskStarted sets
  // render itself right after.
  fresh.render = existing->failedByClient && existing->render;
  *existing = std::move(fresh);
  return *existing;
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
    // A TaskStarted names a new task whatever record its id has: one of a
    // server process since restarted (ids count from 1 again), whether the
    // replay failed it or this client did.
    TaskRecord &record = startOver(started->taskId);
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
  // progress for a record that finished is a new task: a restarted server
  // reusing the id, or the replay speaking of one this client failed at
  // BootstrapBegin. Either way the record starts over, named like one never
  // heard of: the message is the replay's description of a running task.
  const TaskRecord *existing = task(progress.taskId);
  const bool starting = !existing || existing->finished();
  TaskRecord &record =
      starting ? startOver(progress.taskId) : recordFor(progress.taskId);
  if (starting && !progress.message.empty())
    record.label = progress.message; // the replay's description
  record.state = TaskState::Running;
  record.lastProgress.current = progress.current;
  record.lastProgress.total = progress.total;
  record.lastProgress.message = progress.message;
}

// An ending is news unless the server already ended the record: the replay
// repeats endings this client may have seen live.
bool ProjectOps::endingIsNews(const TaskRecord &record)
{
  return !record.finished() || record.failedByClient;
}

void ProjectOps::announceEnding(const TaskRecord &record)
{
  if (!onTaskEnded)
    return;
  // A copy: the callback may clear the finished records.
  const TaskRecord ended = record;
  onTaskEnded(ended);
}

void ProjectOps::handleTaskCompleted(const TaskCompleted &completed)
{
  TaskRecord &record = recordFor(completed.taskId);
  const bool news = endingIsNews(record);
  record.state = TaskState::Completed;
  // The last phase text stays for the panel row; the outcome, when the task
  // has one, replaces it there and is what the completion toast quotes.
  record.outcome = completed.message;
  if (!completed.message.empty())
    record.lastProgress.message = completed.message;
  record.framesCompleted = framesCompletedOf(completed);
  record.error.clear();
  record.failedByClient = false;
  if (news)
    announceEnding(record);
}

void ProjectOps::handleTaskFailed(const TaskFailed &failed)
{
  TaskRecord &record = recordFor(failed.taskId);
  const bool news = endingIsNews(record);
  record.state = TaskState::Failed;
  record.error = failed.error;
  record.framesCompleted = framesCompletedOf(failed); // a cancelled render's
  record.failedByClient = false;
  if (news)
    announceEnding(record);
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
    record.failedByClient = true; // not an ending: the banner says it
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
