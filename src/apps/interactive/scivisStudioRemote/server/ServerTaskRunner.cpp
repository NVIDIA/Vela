// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerTaskRunner.h"
// vsr_scivis_studio_protocol
#include "StudioCodec.h"
#include "TaskMessages.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <exception>
#include <iterator>
#include <string>
#include <utility>

namespace vsr::scivis_studio::server {

using namespace protocol;

namespace {

vsr::network::Message endingOf(uint64_t taskId, const TaskResult &result)
{
  if (result.ok) {
    TaskCompleted completed;
    completed.taskId = taskId;
    completed.message = result.message;
    completed.framesCompleted = result.framesCompleted;
    return encode(completed);
  }
  TaskFailed failed;
  failed.taskId = taskId;
  failed.error = result.error;
  failed.framesCompleted = result.framesCompleted;
  return encode(failed);
}

} // namespace

// TaskControl ////////////////////////////////////////////////////////////////

TaskControl::TaskControl(uint64_t taskId,
    ReportFunction report,
    const std::atomic<uint64_t> *cancelRequested)
    : m_taskId(taskId),
      m_report(std::move(report)),
      m_cancelRequested(cancelRequested)
{}

void TaskControl::operator()(const std::string &phase) const
{
  m_report(0, 0, phase);
}

void TaskControl::operator()(
    uint64_t current, uint64_t total, const std::string &message) const
{
  m_report(current, total, message);
}

bool TaskControl::cancelRequested() const
{
  return m_cancelRequested && m_cancelRequested->load() == m_taskId;
}

uint64_t TaskControl::taskId() const
{
  return m_taskId;
}

// ServerTaskRunner ///////////////////////////////////////////////////////////

ServerTaskRunner::ServerTaskRunner(SendFunction send) : m_send(std::move(send))
{}

uint64_t ServerTaskRunner::enqueue(
    std::string description, TaskBody body, bool exclusive)
{
  QueuedTask task;
  task.id = m_nextTaskId++;
  task.description = std::move(description);
  task.body = std::move(body);
  task.exclusive = exclusive;
  vsr::core::logStatus("[StudioServer] task %llu queued: %s",
      static_cast<unsigned long long>(task.id),
      task.description.c_str());
  m_queue.push_back(std::move(task));
  return m_queue.back().id;
}

bool ServerTaskRunner::cancel(uint64_t taskId, std::string *error)
{
  if (m_running && m_runningId.load() == taskId) {
    // Only reachable from inside the body (the loop is in it otherwise):
    // the running task is cancelled through requestCancelRunning().
    if (error)
      *error = "task already running";
    return false;
  }

  auto itr = std::find_if(m_queue.begin(),
      m_queue.end(),
      [&](const QueuedTask &task) { return task.id == taskId; });
  if (itr == m_queue.end()) {
    if (const auto *finished = findFinished(taskId)) {
      // The cancel this request asked for on the IO thread took effect.
      if (finished->result.cancelled)
        return true;
      if (error)
        *error = "task already finished";
      return false;
    }
    if (error)
      *error = "unknown task " + std::to_string(taskId);
    return false;
  }

  vsr::core::logStatus("[StudioServer] task %llu cancelled: %s",
      static_cast<unsigned long long>(taskId),
      itr->description.c_str());
  FinishedTask finished;
  finished.taskId = taskId;
  finished.description = std::move(itr->description);
  finished.result.ok = false;
  finished.result.error = "cancelled";
  // No body ran, so result.cancelled stays false: this CancelTask is the
  // one answered "ok" here, and a later one finds a task that ended.
  m_queue.erase(itr);
  recordEnding(finished);
  return true;
}

bool ServerTaskRunner::requestCancelRunning(uint64_t taskId)
{
  if (taskId == 0 || m_runningId.load() != taskId)
    return false;
  m_cancelRequested.store(taskId);
  vsr::core::logStatus("[StudioServer] task %llu asked to stop",
      static_cast<unsigned long long>(taskId));
  return true;
}

std::optional<FinishedTask> ServerTaskRunner::runOne()
{
  if (m_queue.empty())
    return {};

  QueuedTask task = std::move(m_queue.front());
  m_queue.pop_front();
  m_running = RunningTask{task.description, 0, 0, task.exclusive};
  m_cancelRequested.store(0);
  m_runningId.store(task.id);
  vsr::core::logStatus("[StudioServer] task %llu started: %s",
      static_cast<unsigned long long>(task.id),
      task.description.c_str());

  const TaskControl control(
      task.id,
      [&](uint64_t current, uint64_t total, const std::string &message) {
        m_running->current = current;
        m_running->total = total;
        TaskProgress event;
        event.taskId = task.id;
        event.current = current;
        event.total = total;
        event.message = message;
        m_send(encode(event));
      },
      &m_cancelRequested);

  FinishedTask finished;
  finished.taskId = task.id;
  finished.description = std::move(task.description);
  try {
    finished.result = task.body(control);
  } catch (const std::exception &e) {
    // A body that throws (a std::filesystem_error for a path the Data Roots
    // admitted but the OS refuses, say) fails its task instead of taking the
    // server down. How far it got is unknown, so the Project counts as
    // changed and the client gets a snapshot to resync from.
    finished.result = TaskResult{};
    finished.result.ok = false;
    finished.result.error = std::string("task aborted: ") + e.what();
    finished.result.projectChanged = true;
  }
  m_runningId.store(0);
  m_running.reset();

  if (finished.result.ok) {
    vsr::core::logStatus("[StudioServer] task %llu completed: %s",
        static_cast<unsigned long long>(task.id),
        finished.description.c_str());
  } else {
    vsr::core::logWarning("[StudioServer] task %llu failed: %s: %s",
        static_cast<unsigned long long>(task.id),
        finished.description.c_str(),
        finished.result.error.c_str());
  }
  recordEnding(finished);
  return finished;
}

void ServerTaskRunner::dropQueued()
{
  const auto kept = std::stable_partition(m_queue.begin(),
      m_queue.end(),
      [](const QueuedTask &task) { return task.exclusive; });
  const auto dropped = size_t(std::distance(kept, m_queue.end()));
  if (dropped > 0) {
    vsr::core::logWarning(
        "[StudioServer] %zu queued task(s) dropped with the session", dropped);
  }
  m_queue.erase(kept, m_queue.end());
  for (const auto &task : m_queue) {
    vsr::core::logStatus(
        "[StudioServer] task %llu kept across the session end: %s",
        static_cast<unsigned long long>(task.id),
        task.description.c_str());
  }
}

void ServerTaskRunner::replayTo(const SendFunction &send)
{
  for (auto &finished : m_finished) {
    if (finished.replayed)
      continue;
    send(endingOf(finished.taskId, finished.result));
    finished.replayed = true;
  }
  if (m_running) {
    TaskProgress event;
    event.taskId = m_runningId.load();
    event.current = m_running->current;
    event.total = m_running->total;
    event.message = m_running->description;
    send(encode(event));
  }
}

size_t ServerTaskRunner::queued() const
{
  return m_queue.size();
}

bool ServerTaskRunner::running() const
{
  return m_running.has_value();
}

bool ServerTaskRunner::exclusivePending() const
{
  if (m_running && m_running->exclusive)
    return true;
  return std::any_of(m_queue.begin(), m_queue.end(), [](const QueuedTask &t) {
    return t.exclusive;
  });
}

const std::deque<FinishedTask> &ServerTaskRunner::finished() const
{
  return m_finished;
}

const std::optional<RunningTask> &ServerTaskRunner::runningTask() const
{
  return m_running;
}

const FinishedTask *ServerTaskRunner::findFinished(uint64_t taskId) const
{
  auto itr = std::find_if(m_finished.begin(),
      m_finished.end(),
      [&](const FinishedTask &task) { return task.taskId == taskId; });
  return itr == m_finished.end() ? nullptr : &*itr;
}

void ServerTaskRunner::recordEnding(const FinishedTask &finished)
{
  m_send(endingOf(finished.taskId, finished.result));
  m_finished.push_back(finished);
  if (m_finished.size() > HISTORY_CAP)
    m_finished.pop_front();
}

} // namespace vsr::scivis_studio::server
