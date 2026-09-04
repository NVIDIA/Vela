// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
// vsr_network
#include "vsr/network/Message.hpp"
// std
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace vsr::scivis_studio::server {

// What a task body reports back: success or an error for the client, an
// optional message for TaskCompleted (a created dataset's id, a render's
// output directory), the task's results subtree when it has one (a render's
// RenderShotResult, set with protocol::setResults()), and whether the body
// stopped because it was asked to. Whether the Project changed is not the
// body's to say: the ProjectContext's revision moves with every mutation.
struct TaskResult
{
  bool ok{true};
  std::string error;
  std::string message;
  protocol::SubtreePtr results;
  // The body stopped because its TaskControl reported a cancel request
  // (with ok=false and error="cancelled"). Decides the CancelTask's answer;
  // see the ServerTaskRunner comment.
  bool cancelled{false};
};

/*
 * A body's handle on its own task. Progress goes out as TaskProgress:
 * indeterminate with phase text ("staging", "installing") or determinate
 * with current/total ("frame 3 of 120"). cancelRequested() is the
 * cooperative cancel flag the IO thread raises through
 * ServerTaskRunner::requestCancelRunning(); a body with a natural boundary
 * (the shot render, once per frame) polls it and stops.
 *
 * Example:
 *   [&](const TaskControl &task) {
 *     for (int frame = 0; frame < frames && !task.cancelRequested(); ++frame) {
 *       task(frame + 1, frames, "frame");
 *       ...
 *     }
 *   }
 */
struct TaskControl
{
  using ReportFunction = std::function<void(
      uint64_t current, uint64_t total, const std::string &message)>;

  TaskControl(uint64_t taskId,
      ReportFunction report,
      const std::atomic<uint64_t> *cancelRequested);

  void operator()(const std::string &phase) const;
  void operator()(
      uint64_t current, uint64_t total, const std::string &message) const;
  bool cancelRequested() const;
  uint64_t taskId() const;

 private:
  uint64_t m_taskId{0};
  ReportFunction m_report;
  const std::atomic<uint64_t> *m_cancelRequested{nullptr};
};

using TaskBody = std::function<TaskResult(const TaskControl &)>;

// A task that ended: what its body returned (or, for a task dropped from
// the queue, the "cancelled" failure the runner made up for it), which is
// what its TaskCompleted/TaskFailed said; kept for the task-status replay
// of the next bootstrap.
struct FinishedTask
{
  uint64_t taskId{0};
  std::string description;
  TaskResult result;
  // Its ending went out in a bootstrap replay; the next one skips it.
  bool replayed{false};
};

// The task running right now: what the replay says of it, and whether it is
// the exclusive one (the shot render) the dispatcher refuses mutations for.
// Its id is the runner's m_runningId, shared with the IO thread.
struct RunningTask
{
  std::string description;
  uint64_t current{0};
  uint64_t total{0};
  bool exclusive{false};
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
 *
 * Cancel is cooperative: a task still in the queue is dropped and reported
 * as TaskFailed{"cancelled"}; for the running one the IO thread raises the
 * cancel flag (requestCancelRunning) that a body polls through its
 * TaskControl. The body reports whether it stopped for the flag
 * (TaskResult::cancelled, with error "cancelled"; the shot render does, at
 * its next frame). The CancelTask itself, dispatched once the body has
 * returned, is answered "ok" when the body reported cancelled and "task
 * already finished" otherwise: the body ignored the flag and completed
 * (every other body today), or failed for a reason of its own. A task
 * dropped from the queue leaves cancelled false (no body ran): the
 * CancelTask that dropped it is answered on the spot, and a later one is
 * told the task finished. An exclusive task (the shot render) makes the
 * dispatcher refuse mutating requests while it is queued or running.
 *
 * The runner remembers the last HISTORY_CAP tasks that ended, so a client
 * that connects after a task finished with nobody listening still hears how
 * it ended: the bootstrap replays each ending not replayed before, verbatim,
 * then a TaskProgress describing the running task if there is one. A
 * replayed ending stays known, so a CancelTask naming it is still told the
 * task finished rather than that it never existed. Task ids increase
 * monotonically for the life of the runner and are never reused.
 *
 * Example:
 *   const auto taskId = runner.enqueue("import 'a.obj'", [&](auto &task) {
 *     task("importing");
 *     TaskResult result;
 *     result.ok = context.addStaticDataset(...) != nullptr;
 *     return result;
 *   });
 *   setResults(reply, TaskStartedResult{taskId});
 *   ...
 *   runner.runOne(); // once per loop iteration
 */
struct ServerTaskRunner
{
  using SendFunction = std::function<void(vsr::network::Message &&)>;

  // Finished tasks kept for the replay; older ones are forgotten first.
  static constexpr size_t HISTORY_CAP = 32;

  explicit ServerTaskRunner(SendFunction send);

  // Queues `body` and returns its task id; nothing is sent until it runs.
  uint64_t enqueue(
      std::string description, TaskBody body, bool exclusive = false);

  // Loop thread. Removes a queued task (TaskFailed{"cancelled"} is sent), or
  // acknowledges the cancel of a task whose body reported it stopped when
  // asked to. False with `error` for the running task, a task that finished
  // (completed, or failed on its own), or an unknown id.
  bool cancel(uint64_t taskId, std::string *error = nullptr);

  // Any thread. Raises the cancel flag when `taskId` is the running task;
  // false when it is not.
  bool requestCancelRunning(uint64_t taskId);

  // Runs the oldest queued task to completion, sending its progress and its
  // one TaskCompleted/TaskFailed, and returns its ending; empty when the
  // queue was empty.
  std::optional<FinishedTask> runOne();

  // Forgets every queued task without a word: the session they were sent on
  // is gone and their ids mean nothing to the next client. An exclusive task
  // (the shot render) is kept: like a running one it outlives its session,
  // and the next bootstrap's replay reports how it ended.
  void dropQueued();

  // The task-status replay: every ending not replayed before, verbatim, then
  // the running task as a TaskProgress whose message is its description.
  // The endings stay in finished(), marked replayed.
  void replayTo(const SendFunction &send);

  size_t queued() const;
  bool running() const;
  // A task flagged exclusive is queued or running.
  bool exclusivePending() const;
  const std::deque<FinishedTask> &finished() const;
  const std::optional<RunningTask> &runningTask() const;

 private:
  struct QueuedTask
  {
    uint64_t id{0};
    std::string description;
    TaskBody body;
    bool exclusive{false};
  };

  const FinishedTask *findFinished(uint64_t taskId) const;
  // Sends the ending and keeps it for the replay, forgetting the oldest
  // beyond HISTORY_CAP.
  void recordEnding(const FinishedTask &finished);

  SendFunction m_send;
  std::deque<QueuedTask> m_queue;
  uint64_t m_nextTaskId{1};
  std::optional<RunningTask> m_running;
  std::deque<FinishedTask> m_finished;

  // Shared with the IO thread: the running task's id (0 = none) and the id a
  // cancel was requested for (0 = none).
  std::atomic<uint64_t> m_runningId{0};
  std::atomic<uint64_t> m_cancelRequested{0};
};

} // namespace vsr::scivis_studio::server
