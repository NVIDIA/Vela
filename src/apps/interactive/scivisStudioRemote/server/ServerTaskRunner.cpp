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
#include <string>
#include <utility>

namespace vsr::scivis_studio::server {

using namespace protocol;

ServerTaskRunner::ServerTaskRunner(SendFunction send) : m_send(std::move(send))
{}

uint64_t ServerTaskRunner::enqueue(std::string description, TaskBody body)
{
  QueuedTask task;
  task.id = m_nextTaskId++;
  task.description = std::move(description);
  task.body = std::move(body);
  vsr::core::logStatus("[StudioServer] task %llu queued: %s",
      static_cast<unsigned long long>(task.id),
      task.description.c_str());
  m_queue.push_back(std::move(task));
  return m_queue.back().id;
}

bool ServerTaskRunner::cancel(uint64_t taskId, std::string *error)
{
  if (m_running && *m_running == taskId) {
    if (error)
      *error = "task already running";
    return false;
  }

  auto itr = std::find_if(m_queue.begin(),
      m_queue.end(),
      [&](const QueuedTask &task) { return task.id == taskId; });
  if (itr == m_queue.end()) {
    if (error)
      *error = "unknown task " + std::to_string(taskId);
    return false;
  }

  vsr::core::logStatus("[StudioServer] task %llu cancelled: %s",
      static_cast<unsigned long long>(taskId),
      itr->description.c_str());
  m_queue.erase(itr);
  TaskFailed failed;
  failed.taskId = taskId;
  failed.error = "cancelled";
  m_send(encode(failed));
  return true;
}

std::optional<RanTask> ServerTaskRunner::runOne()
{
  if (m_queue.empty())
    return {};

  QueuedTask task = std::move(m_queue.front());
  m_queue.pop_front();
  m_running = task.id;
  vsr::core::logStatus("[StudioServer] task %llu started: %s",
      static_cast<unsigned long long>(task.id),
      task.description.c_str());

  const auto progress = [&](const std::string &phase) {
    TaskProgress event;
    event.taskId = task.id;
    event.message = phase;
    m_send(encode(event));
  };

  RanTask ran;
  ran.taskId = task.id;
  try {
    ran.result = task.body(progress);
  } catch (const std::exception &e) {
    // A body that throws (a std::filesystem_error for a path the Data Roots
    // admitted but the OS refuses, say) fails its task instead of taking the
    // server down. How far it got is unknown, so the Project counts as
    // changed and the client gets a snapshot to resync from.
    ran.result = TaskResult{};
    ran.result.ok = false;
    ran.result.error = std::string("task aborted: ") + e.what();
    ran.result.projectChanged = true;
  }
  m_running.reset();

  if (ran.result.ok) {
    vsr::core::logStatus("[StudioServer] task %llu completed: %s",
        static_cast<unsigned long long>(task.id),
        task.description.c_str());
    TaskCompleted completed;
    completed.taskId = task.id;
    completed.message = ran.result.message;
    m_send(encode(completed));
  } else {
    vsr::core::logWarning("[StudioServer] task %llu failed: %s: %s",
        static_cast<unsigned long long>(task.id),
        task.description.c_str(),
        ran.result.error.c_str());
    TaskFailed failed;
    failed.taskId = task.id;
    failed.error = ran.result.error;
    m_send(encode(failed));
  }
  return ran;
}

void ServerTaskRunner::dropQueued()
{
  if (!m_queue.empty()) {
    vsr::core::logWarning(
        "[StudioServer] %zu queued task(s) dropped with the session",
        m_queue.size());
  }
  m_queue.clear();
}

size_t ServerTaskRunner::queued() const
{
  return m_queue.size();
}

bool ServerTaskRunner::running() const
{
  return m_running.has_value();
}

} // namespace vsr::scivis_studio::server
