// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_network
#include "vsr/network/Message.hpp"
// std
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace vsr::scivis_studio::server {

// What a task body reports back: success or an error for the client, an
// optional message for TaskCompleted (a created dataset's id), and whether
// the Project changed so the caller knows to send a ProjectSnapshot.
struct TaskResult
{
  bool ok{true};
  std::string error;
  std::string message;
  bool projectChanged{false};
};

// Task bodies call `progress` at coarse phase boundaries ("staging",
// "installing"); it becomes an indeterminate TaskProgress.
using TaskProgressFunction = std::function<void(const std::string &phase)>;
using TaskBody = std::function<TaskResult(const TaskProgressFunction &)>;

// A task that ran, for the caller's follow-up (snapshot).
struct RanTask
{
  uint64_t taskId{0};
  TaskResult result;
};

/*
 * Single-lane Server Task runner for the render loop. Task-launching
 * requests enqueue a body here and are answered with the new task id
 * (TaskStartedResult) right away; the loop then calls runOne() once per
 * iteration, between applying the Control-State Latch and rendering, so a
 * task runs to completion on the loop thread while frames pause. The runner
 * pushes the task's TaskProgress events and exactly one TaskCompleted or
 * TaskFailed. A body that throws is caught here and reported as TaskFailed
 * with the exception's message, so no request can bring the loop down.
 * Cancel is cooperative and coarse: a task still in the queue is dropped and
 * reported as TaskFailed{"cancelled"}; the running one cannot be interrupted
 * (no ProjectContext operation has a cancel hook yet). Task ids increase
 * monotonically for the life of the runner and are never reused.
 *
 * Example:
 *   const auto taskId = runner.enqueue("import 'a.obj'", [&](auto &progress) {
 *     progress("importing");
 *     TaskResult result;
 *     result.ok = context.addStaticDataset(...) != nullptr;
 *     result.projectChanged = true;
 *     return result;
 *   });
 *   setResults(reply, TaskStartedResult{taskId});
 *   ...
 *   if (auto ran = runner.runOne(); ran && ran->result.projectChanged)
 *     sendSnapshot();
 */
struct ServerTaskRunner
{
  using SendFunction = std::function<void(vsr::network::Message &&)>;

  explicit ServerTaskRunner(SendFunction send);

  // Queues `body` and returns its task id; nothing is sent until it runs.
  uint64_t enqueue(std::string description, TaskBody body);

  // Removes a queued task (TaskFailed{"cancelled"} is sent). False with
  // `error` for the running task or an id that is not queued.
  bool cancel(uint64_t taskId, std::string *error = nullptr);

  // Runs the oldest queued task to completion, sending its progress and its
  // one TaskCompleted/TaskFailed; empty when the queue was empty.
  std::optional<RanTask> runOne();

  // Forgets every queued task without a word: the session they were sent on
  // is gone and their ids mean nothing to the next client.
  void dropQueued();

  size_t queued() const;
  bool running() const;

 private:
  struct QueuedTask
  {
    uint64_t id{0};
    std::string description;
    TaskBody body;
  };

  SendFunction m_send;
  std::deque<QueuedTask> m_queue;
  uint64_t m_nextTaskId{1};
  std::optional<uint64_t> m_running;
};

} // namespace vsr::scivis_studio::server
