// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Script.h"
#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "PlaybackMessages.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "StudioProtocol.h"
#include "ViewportMessages.h"
// vsr_core
#include "vsr/core/FlatMap.hpp"
// std
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio {
struct Shot;
}

namespace vsr::scivis_studio::test_client {

struct RunnerOptions
{
  // Where `connect` goes when the command names no host or port.
  std::string host{"127.0.0.1"};
  int port{protocol::DEFAULT_PORT};
  // Deadline of every waiting command unless it carries `timeout=<ms>`.
  std::chrono::milliseconds timeout{5000};
  // Carry on after a FAIL instead of stopping the script.
  bool keepGoing{false};
  // Drop the `EVT` lines for server messages; the `dump-*` commands still
  // print theirs.
  bool quietEvents{false};
};

/*
 * Executes script commands against a TestSession and writes the record
 * stream: exactly one `OK <command>` or `FAIL <command>: <reason>` line per
 * command, and one `EVT <Name> key=value ...` line for every server message
 * as the session consumes it. Every line is one record, so a shell script or
 * a reader can grep the output.
 *
 * The command vocabulary is the server surface through milestone 7: session
 * (connect, disconnect, shutdown, ping, expect-pong, await-lost, reconnect,
 * sleep, expect-error, send-raw), rendering (set-frame-config, set-encodings,
 * start-rendering, stop-rendering, await-frame, save-frame), scene edits
 * (set-param, remove-param, set-node-transform), one request command per
 * Project Op, Remote Browse and task message (new-project, create-shot,
 * list-directory, cancel-task, set-playing, request-array-histogram,
 * render-shot, ...), the waits that go with them (await-task,
 * await-task-progress, await-snapshot, await-reply), playback and the
 * viewport (set-time, await-frame-at, await-frame-advance, await-warning,
 * pick, set-outline, viewport-settings), the UI state round trip
 * (set-ui-state, dump-ui-state), inspection (dump-scene, dump-layers,
 * dump-project, dump-frame, find-object) and `assert <value> <op> <rhs>` over
 * the named values assertNames() lists.
 * Waiting commands take a trailing `timeout=<ms>` and FAIL as soon as the
 * connection is Lost. An Error the server sends while any command but
 * expect-error runs FAILs that command: only Project Ops carry request ids, so
 * for everything else the command in flight is the best attribution there is.
 *
 * A request command mints a request id, sends, awaits the ProjectOpReply with
 * that id and prints it with the decoded result fields; `ok=false` is a FAIL
 * unless the command is prefixed with `expect-fail`, when `ok=true` is. The
 * ids replies mint land in variables ($lastShotId, $lastTaskId, ...) that
 * `$name` expands anywhere in a later command's arguments. The `no-wait`
 * prefix sends without awaiting, so several requests can be in flight (a
 * task to cancel before it runs); `await-reply` collects them.
 *
 * Example:
 *   TestSession session;
 *   CommandRunner runner(&session, &std::cout, options);
 *   std::vector<Command> commands;
 *   parseScript("connect; start-rendering; await-frame 3", commands);
 *   return runner.run(commands) ? 0 : 1;
 */
struct CommandRunner
{
  CommandRunner(
      TestSession *session, std::ostream *out, RunnerOptions options = {});

  // Runs the commands in order, stopping at the first FAIL unless keepGoing.
  // True iff every command printed OK.
  bool run(const std::vector<Command> &commands);

  // Every value `assert` can name, as documented in --help; the pattern
  // param.<type>.<index>.<name> is listed as written.
  static const std::vector<std::string> &assertNames();
  // One line per command for --help.
  static const std::vector<std::string> &commandHelp();

 private:
  // Why a command FAILed; empty when it is OK.
  using Failure = std::optional<std::string>;
  using Deadline = std::chrono::milliseconds;
  // Decodes an ok reply's results for the record stream: appends the result
  // fields to the reply's Event, adds the records that follow it (one per
  // directory entry, say), remembers minted ids in variables, and FAILs a
  // reply that lacks the results its request promised.
  using Describe = std::function<Failure(
      const protocol::ProjectOpReply &, Event &, std::vector<Event> &)>;
  // How a wait ended: with what it waited for, at the deadline, with the link
  // Lost meanwhile, or with an Error nobody expected.
  enum class Wait
  {
    Done,
    TimedOut,
    Lost,
    Error
  };
  // Whether a loss during the wait ends it.
  enum class LossEnds
  {
    Wait,
    Nothing
  };

  // One command: its events, then its OK or FAIL record. True on OK.
  bool runCommand(const Command &command);
  Failure execute(Command command);

  Failure connect(const Command &, Deadline);
  Failure disconnect(const Command &);
  Failure shutdown(const Command &, Deadline);
  Failure ping(const Command &);
  Failure expectPong(const Command &, Deadline);
  Failure awaitLost(const Command &, Deadline);
  Failure reconnect(const Command &, Deadline);
  Failure sleep(const Command &);
  Failure expectError(const Command &, Deadline);
  Failure sendRaw(const Command &);
  Failure setFrameConfig(const Command &, Deadline);
  Failure setEncodings(const Command &);
  Failure startRendering(const Command &);
  Failure stopRendering(const Command &);
  Failure awaitFrame(const Command &, Deadline);
  Failure saveFrame(const Command &);
  Failure awaitFrameAt(const Command &, Deadline);
  Failure awaitFrameAdvance(const Command &, Deadline);
  Failure awaitWarning(const Command &, Deadline);
  Failure setTime(const Command &);
  Failure pick(const Command &, Deadline);
  Failure setOutline(const Command &);
  Failure viewportSettings(const Command &);
  Failure findObject(const Command &);
  Failure setParam(const Command &);
  Failure removeParam(const Command &);
  Failure setNodeTransform(const Command &);
  Failure dumpScene(const Command &);
  Failure dumpLayers(const Command &);
  Failure dumpProject(const Command &);
  Failure dumpFrame(const Command &);
  Failure setUIState(const Command &);
  Failure dumpUIState(const Command &);
  Failure assertValue(const Command &);

  // Request commands, waits and prefixes (the milestone-5 surface)
  Failure executeRequest(const Command &, Deadline, bool &handled);
  Failure openProject(const Command &, Deadline);
  Failure saveProject(const Command &, Deadline);
  Failure importStaticDataset(const Command &, Deadline);
  Failure importFileAnimationDataset(const Command &, Deadline);
  Failure declareFileAnimationDataset(const Command &, Deadline);
  Failure removeDataset(const Command &, Deadline);
  Failure discoverDatasetCandidates(const Command &, Deadline);
  Failure incorporateDatasetCandidate(const Command &, Deadline);
  Failure updateShot(const Command &, Deadline);
  Failure setPlaying(const Command &, Deadline);
  Failure requestArrayHistogram(const Command &, Deadline);
  Failure addLight(const Command &, Deadline);
  Failure removeLight(const Command &, Deadline);
  Failure listRoots(const Command &, Deadline);
  Failure listDirectory(const Command &, Deadline);
  Failure renderShot(const Command &, Deadline);
  Failure cancelTask(const Command &, Deadline);
  Failure awaitTask(const Command &, Deadline);
  Failure awaitTaskProgress(const Command &, Deadline);
  Failure awaitSnapshot(const Command &, Deadline);
  Failure awaitReply(const Command &, Deadline);

  // The recurring request shapes: {}, {name}, {id}, {id, newName},
  // {id, file} and {file}, each from the command's arguments.
  template <typename R>
  Failure bareRequest(const Command &, Deadline, const Describe & = {});
  template <typename R>
  Failure nameRequest(
      const Command &, Deadline, std::string R::*name, const Describe & = {});
  template <typename R>
  Failure idRequest(const Command &,
      Deadline,
      std::string R::*id,
      const char *idName,
      const Describe & = {});
  template <typename R>
  Failure renameRequest(
      const Command &, Deadline, std::string R::*id, const char *idName);
  template <typename R>
  Failure saveArchiveRequest(const Command &,
      Deadline,
      std::string R::*id,
      const char *idName,
      const Describe & = {});
  template <typename R>
  Failure loadArchiveRequest(const Command &, Deadline, const Describe &);

  // Mints the request id, sends, and either awaits the reply (see
  // awaitReply(uint64_t)) or, under `no-wait`, parks the Describe for a
  // later await-reply.
  template <typename R>
  Failure sendRequest(R request, Deadline, const Describe & = {});
  // Waits for the ProjectOpReply with that id, prints it with `describe`'s
  // fields, and turns `ok` into the OK/FAIL the expect-fail prefix asks for.
  Failure awaitReply(uint64_t requestId, Deadline, const Describe &describe);
  // A Describe that reads one string id out of a Result, prints it as
  // `key=` and stores it in the named variable.
  template <typename Result>
  Describe createdResult(
      std::string Result::*id, const char *key, const char *variable);
  // What a task's completion message carries, remembered per task id at the
  // launch reply so await-task knows which variable the message fills.
  enum class TaskMessage
  {
    Other,
    DatasetId
  };
  // The Describe of every task-launching request: `taskId=`, $lastTaskId,
  // and the kind of message the task will complete with.
  Describe taskStarted(TaskMessage message = TaskMessage::Other);
  // The value a `$name` expands to; empty when there is no such variable.
  std::optional<std::string> variable(const std::string &name) const;
  // The replica's Shot with that id (`active` names the active shot), or
  // null with the reason.
  const Shot *replicaShot(const std::string &id, std::string &error) const;
  // The shot id a command named, `active` resolved through the replica;
  // empty with the reason when there is no replica to resolve it against.
  std::optional<std::string> shotIdArgument(
      const std::string &text, std::string &error) const;
  // The task id a command named, or `$lastTaskId` when it named none; empty
  // with the reason when neither is there.
  std::optional<uint64_t> taskIdArgument(
      const std::vector<std::string> &args, std::string &error) const;

  // The current text of a named value; empty with the reason when it is
  // unknown or not available yet (no frame, no replica, ...).
  std::optional<std::string> namedValue(
      const std::string &name, std::string &error);

  // Polls, printing events as they come, until `done` holds, the deadline
  // passes, the link goes Lost (unless LossEnds::Nothing) or an Error arrives.
  Wait pumpUntil(const std::function<bool()> &done,
      Deadline deadline,
      LossEnds lossEnds = LossEnds::Wait);
  // Polls until an event `accept`s (that event is returned in `matched`) or
  // `done` holds; an Error that `accept` does not take ends the wait as
  // Wait::Error. `accept` sees each event before it is printed and may add
  // fields to it (a reply's decoded results).
  Wait pumpUntilEvent(const std::function<bool(Event &)> &accept,
      Deadline deadline,
      Event *matched = nullptr,
      LossEnds lossEnds = LossEnds::Wait,
      const std::function<bool()> &done = {});
  // The FAIL reason of a wait that ended without `awaited`.
  std::string waitFailure(
      Wait wait, const std::string &awaited, Deadline deadline) const;
  // Prints whatever events are already queued without waiting; the reason
  // when one of them was an Error, which fails the command that drained it.
  Failure drainEvents();
  void printEvent(const Event &event);
  void printRecord(const std::string &line);

  TestSession *m_session{nullptr};
  std::ostream *m_out{nullptr};
  RunnerOptions m_options;

  // The prefixes of the command being executed.
  bool m_expectFail{false};
  bool m_noWait{false};
  // Ids the replies minted, by variable name (lastShotId, lastTaskId, ...).
  vsr::core::FlatMap<std::string, std::string> m_variables;
  // What each launched task's completion message will carry, by task id; a
  // reused id is the newer task's.
  vsr::core::FlatMap<uint64_t, TaskMessage> m_taskMessages;
  // Requests sent under no-wait whose replies are still to be collected, in
  // send order, with the Describe each await-reply will use.
  std::deque<std::pair<uint64_t, Describe>> m_pendingReplies;
  // What await-snapshot waits past: the snapshots received when the last
  // awaited thing arrived -- a request's reply (or, until a no-wait reply is
  // collected, its send), a task's end message, the previous await-snapshot.
  // Taken at the reply rather than the send, since the previous request's
  // snapshot may still be on the wire at the send. Each await-snapshot then
  // moves the mark on by one, so consecutive awaits consume consecutive
  // snapshots.
  size_t m_snapshotMark{0};
  // What the last list-directory returned (`browse.entries`).
  std::vector<protocol::DirectoryEntry> m_browseEntries;
  // The last ViewportSettings sent: every viewport-settings command edits
  // this copy and sends it whole, so the commands compose.
  protocol::ViewportSettings m_viewportSettings;
  // The reply of the last pick, and the result of the last ok histogram.
  std::optional<protocol::PickReply> m_lastPick;
  std::optional<protocol::ArrayHistogramResult> m_histogram;
  // The UI state tree set-ui-state builds (`windows/<key>` string leaves),
  // sent with every save-project from then on; null until the first
  // set-ui-state and after `set-ui-state none`, when save-project sends none.
  protocol::SubtreePtr m_uiStateToSave;
};

} // namespace vsr::scivis_studio::test_client
