// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/*
 * The CommandRunner waits on requests and Server Tasks: await-task,
 * await-task-progress, await-snapshot and await-reply, and the reply wait
 * every request command ends in. The table in CommandRunner.cpp has checked
 * each command's argument count and prefixes before a handler runs.
 */

#include "CommandRunner.h"
#include "CommandText.h"
// std
#include <algorithm>

namespace vsr::scivis_studio::test_client {

using namespace protocol;

// Tasks //////////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::awaitTask(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  std::string error;
  const auto id = taskIdArgument(command.args, error);
  if (!id)
    return error;
  const auto taskId = *id;
  if (const auto lost = notConnected())
    return lost;

  // The progress prints as it comes (determinate `current= total=` for a
  // render); the end record carries the message and, for a render, the
  // `framesCompleted=`.
  const auto ended = [&] {
    const auto *task = m_session->task(taskId);
    return task && task->finished();
  };
  const auto wait = pumpUntil(ended, deadline);
  if (wait != Wait::Done) {
    return waitFailure(
        wait, "the end of task " + std::to_string(taskId), deadline);
  }
  const auto &task = *m_session->task(taskId);
  // The task's snapshot follows its end message: await-snapshot waits past
  // whatever had arrived by then.
  m_snapshotMark = task.snapshotsAtEnd;
  const bool failed = task.status == TaskRecord::Status::Failed;
  if (failed && !modifiers.expectFail)
    return "task " + std::to_string(taskId) + " failed: " + task.message;
  if (!failed && modifiers.expectFail) {
    return "task " + std::to_string(taskId)
        + " completed, but was expected to fail";
  }
  // The completion message: a dataset-producing task's is the new dataset's
  // id, a render's the output directory. Which it is was recorded at the
  // launch reply; a task this runner did not launch fills only the message.
  if (!failed) {
    m_variables["lastTaskMessage"] = task.message;
    const auto *message = m_taskMessages.at(taskId);
    if (message && *message == TaskMessage::DatasetId)
      m_variables["lastDatasetId"] = task.message;
  }
  return {};
}

CommandRunner::Failure CommandRunner::awaitTaskProgress(
    const Command &command, Deadline deadline)
{
  std::string error;
  const auto id = taskIdArgument(command.args, error);
  if (!id)
    return error;
  if (const auto lost = notConnected())
    return lost;
  // Until the task has reported progress at least once: a report an earlier
  // command already drained counts, so an assert between the launch and this
  // wait costs nothing. A task that ends first will report no more, so its
  // end stops the wait, as a FAIL when it never reported.
  const auto idText = std::to_string(*id);
  const auto wait = pumpUntil(
      [&] {
        const auto *task = m_session->task(*id);
        return task && (task->progressReports > 0 || task->finished());
      },
      deadline);
  if (wait != Wait::Done)
    return waitFailure(wait, "progress of task " + idText, deadline);
  const auto &task = *m_session->task(*id);
  if (task.progressReports == 0) {
    return "task " + idText + " ended (" + toString(task.status)
        + ") before reporting progress";
  }
  return {};
}

CommandRunner::Failure CommandRunner::awaitSnapshot(
    const Command &, Deadline deadline)
{
  if (const auto lost = notConnected())
    return lost;
  const auto wait =
      pumpUntil([&] { return m_session->snapshotsReceived() > m_snapshotMark; },
          deadline);
  if (wait != Wait::Done)
    return waitFailure(wait, "ProjectSnapshot", deadline);
  // One snapshot per await: two that land in the same poll (a SetPlaying's
  // and the auto-stop's, say) are awaited one at a time, the second at once.
  ++m_snapshotMark;
  return {};
}

// Replies ////////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::awaitReply(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  unsigned long long requestId = 0;
  if (!command.args.empty() && !parseNonNegative(command.args[0], requestId))
    return usageError(command);
  if (m_pendingReplies.empty())
    return "no no-wait request is awaiting its reply";
  auto pending = m_pendingReplies.begin();
  if (!command.args.empty()) {
    pending = std::find_if(m_pendingReplies.begin(),
        m_pendingReplies.end(),
        [&](const auto &p) { return p.first == requestId; });
    if (pending == m_pendingReplies.end()) {
      return "request " + std::to_string(requestId)
          + " was not sent with no-wait, or its reply was collected already";
    }
  }
  const auto id = pending->first;
  const auto describe = std::move(pending->second);
  m_pendingReplies.erase(pending);
  return awaitReply(id, deadline, modifiers, describe);
}

CommandRunner::Failure CommandRunner::awaitReply(uint64_t requestId,
    Deadline deadline,
    Modifiers modifiers,
    const Describe &describe)
{
  const auto idText = std::to_string(requestId);
  std::vector<Event> following;
  Failure described;

  // Runs `describe` on an ok reply, so the reply's record shows the results.
  const auto decorate = [&](const ProjectOpReply &reply, Event &event) {
    if (reply.ok && describe)
      described = describe(*this, reply, event, following);
  };

  const auto *reply = m_session->reply(requestId);
  if (reply) {
    // Already consumed and printed (a no-wait reply an earlier command
    // drained): the results are decoded, the record is not repeated.
    Event event{"ProjectOpReply", {}};
    decorate(*reply, event);
  } else {
    const auto wait = pumpUntilEvent(
        [&](Event &event) {
          if (event.name != "ProjectOpReply")
            return false;
          for (const auto &[key, value] : event.fields) {
            if (key == "requestId" && value == idText) {
              reply = m_session->reply(requestId);
              if (reply)
                decorate(*reply, event);
              return true;
            }
          }
          return false;
        },
        deadline);
    if (wait != Wait::Done)
      return waitFailure(wait, "the reply to request " + idText, deadline);
    if (!reply)
      return "the reply to request " + idText + " did not decode";
  }
  // A snapshot this request caused follows its reply; whatever arrived up to
  // the reply belongs to earlier requests.
  m_snapshotMark =
      m_session->snapshotsAtReply(requestId).value_or(m_snapshotMark);
  for (const auto &event : following)
    printEvent(event);

  if (!reply->ok && !modifiers.expectFail)
    return "server refused: " + reply->error;
  if (reply->ok && modifiers.expectFail)
    return "expected the request to fail, but the server accepted it";
  return described;
}

std::optional<uint64_t> CommandRunner::taskIdArgument(
    const std::vector<std::string> &args, std::string &error) const
{
  unsigned long long taskId = 0;
  if (!args.empty()) {
    if (!parseNonNegative(args[0], taskId)) {
      error = "not a task id: " + args[0];
      return {};
    }
    return taskId;
  }
  const auto last = variable("lastTaskId");
  if (!last) {
    error = "no task has been started yet ($lastTaskId is unset)";
    return {};
  }
  parseNonNegative(*last, taskId);
  return taskId;
}

} // namespace vsr::scivis_studio::test_client
