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
#include <utility>
#include <vector>

namespace vsr::scivis_studio {
struct Shot;
}

namespace vsr::scivis_studio::test_client {

struct RunnerOptions
{
  // Where `connect` goes when the command names no host or port.
  std::string host{"127.0.0.1"};
  uint16_t port{protocol::DEFAULT_PORT};
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
 * The command vocabulary is the table commands() returns: one CommandSpec
 * per command with its name, usage, arity and kind, the one place those are
 * written. execute() finds the row, checks the prefixes and the argument
 * count against it, and runs the handler; usage FAILs, --help and the
 * README's command table all print from the same rows. The handlers are
 * grouped by file: SessionCommands.cpp (the connection and the frame
 * stream), SceneCommands.cpp (scene edits, playback, the viewport,
 * inspection, UI state, assert), RequestCommands.cpp (one request command
 * per Project Op, Remote Browse and task message) and WaitCommands.cpp (the
 * waits on tasks and replies); NamedValues.cpp holds what `assert` can name.
 *
 * Waiting commands take a trailing `timeout=<ms>` and FAIL as soon as the
 * connection is Lost. An Error the server sends while any command but
 * expect-error runs FAILs that command: only Project Ops carry request ids,
 * so for everything else the command in flight is the best attribution there
 * is.
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
  // Why a command FAILed; empty when it is OK.
  using Failure = std::optional<std::string>;
  using Deadline = std::chrono::milliseconds;

  // The prefixes a command was written with: `expect-fail` makes the refused
  // outcome the OK one, `no-wait` sends without awaiting the reply.
  struct Modifiers
  {
    bool expectFail{false};
    bool noWait{false};
  };

  // What a command is, which decides the prefixes it takes: a Request takes
  // both, a Wait (on a task or a reply, whose outcome can be a failure) takes
  // expect-fail, a Session command takes none.
  enum class Kind
  {
    Session,
    Request,
    Wait
  };

  // A handler as the table holds it: a member of one of three shapes, each
  // taking only what it uses, or a request shape bound to its request type.
  struct Run
  {
    using Fn = std::function<Failure(
        CommandRunner &, const Command &, Deadline, Modifiers)>;

    Run(Failure (CommandRunner::*handler)(const Command &));
    Run(Failure (CommandRunner::*handler)(const Command &, Deadline));
    Run(Failure (CommandRunner::*handler)(
        const Command &, Deadline, Modifiers));
    Run(Fn fn);

    Fn fn;
  };

  // One row of the command table: the one place a command's name, usage and
  // arity are written. execute() checks the argument count and the prefixes
  // against the row before the handler runs, so a handler sees only the
  // arities its usage admits.
  struct CommandSpec
  {
    const char *name;
    // The arguments as --help shows them: `<required>`, `[optional]`, `...`
    // for a list.
    const char *usage;
    int minArgs;
    // -1 for no upper bound.
    int maxArgs;
    Kind kind;
    Run run;
    // What the command does, in one line: the --help and README text.
    const char *summary;
  };

  CommandRunner(
      TestSession *session, std::ostream *out, RunnerOptions options = {});

  // Runs the commands in order, stopping at the first FAIL unless keepGoing.
  // True iff every command printed OK.
  bool run(const std::vector<Command> &commands);

  // Every command, sorted by name.
  static const std::vector<CommandSpec> &commands();
  // The row of that command; null when there is none.
  static const CommandSpec *findCommand(const std::string &name);
  // Every value `assert` can name, as documented in --help; the pattern
  // param.<type>.<index>.<name> is listed as written.
  static const std::vector<std::string> &assertNames();

 private:
  // Decodes an ok reply's results for the record stream: appends the result
  // fields to the reply's Event, adds the records that follow it (one per
  // directory entry, say), remembers minted ids in the runner's variables,
  // and FAILs a reply that lacks the results its request promised.
  using Describe = std::function<Failure(CommandRunner &,
      const protocol::ProjectOpReply &,
      Event &,
      std::vector<Event> &)>;
  // How a wait ended: with what it waited for, at the deadline, with the link
  // Lost meanwhile, or with an Error nobody expected.
  enum class WaitEnd
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
  // What a task's completion message carries, remembered per task id at the
  // launch reply so await-task knows which variable the message fills.
  enum class TaskMessage
  {
    Other,
    DatasetId
  };

  // One command: its events, then its OK or FAIL record. True on OK.
  bool runCommand(const Command &command);
  Failure execute(Command command);
  // The FAIL of a command whose arguments do not fit its row's usage.
  static std::string usageError(const Command &command);

  // The connection and the frame stream (SessionCommands.cpp)
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
  Failure awaitFrameAt(const Command &, Deadline);
  Failure awaitFrameAdvance(const Command &, Deadline);
  Failure awaitWarning(const Command &, Deadline);
  Failure saveFrame(const Command &);

  // Scene edits, playback, the viewport, inspection, UI state and assert
  // (SceneCommands.cpp)
  Failure setParam(const Command &);
  Failure removeParam(const Command &);
  Failure setNodeTransform(const Command &);
  Failure setTime(const Command &);
  Failure pick(const Command &, Deadline);
  Failure setOutline(const Command &);
  Failure viewportSettings(const Command &);
  Failure findObject(const Command &);
  Failure dumpScene(const Command &);
  Failure dumpLayers(const Command &);
  Failure dumpProject(const Command &);
  Failure dumpFrame(const Command &);
  Failure setUIState(const Command &);
  Failure dumpUIState(const Command &);
  Failure assertValue(const Command &);

  // The request commands with arguments of their own (RequestCommands.cpp);
  // the rest are request shapes bound in the table.
  Failure openProject(const Command &, Deadline, Modifiers);
  Failure saveProject(const Command &, Deadline, Modifiers);
  Failure importStaticDataset(const Command &, Deadline, Modifiers);
  Failure importFileAnimationDataset(const Command &, Deadline, Modifiers);
  Failure declareFileAnimationDataset(const Command &, Deadline, Modifiers);
  Failure removeDataset(const Command &, Deadline, Modifiers);
  Failure discoverDatasetCandidates(const Command &, Deadline, Modifiers);
  Failure incorporateDatasetCandidate(const Command &, Deadline, Modifiers);
  Failure updateShot(const Command &, Deadline, Modifiers);
  Failure setPlaying(const Command &, Deadline, Modifiers);
  Failure requestArrayHistogram(const Command &, Deadline, Modifiers);
  Failure addLight(const Command &, Deadline, Modifiers);
  Failure removeLight(const Command &, Deadline, Modifiers);
  Failure createColorMap(const Command &, Deadline, Modifiers);
  Failure listRoots(const Command &, Deadline, Modifiers);
  Failure listDirectory(const Command &, Deadline, Modifiers);
  Failure renderShot(const Command &, Deadline, Modifiers);
  Failure cancelTask(const Command &, Deadline, Modifiers);

  // The waits (WaitCommands.cpp)
  Failure awaitTask(const Command &, Deadline, Modifiers);
  Failure awaitTaskProgress(const Command &, Deadline);
  Failure awaitSnapshot(const Command &, Deadline);
  Failure awaitReply(const Command &, Deadline, Modifiers);

  // The recurring request shapes as table rows: {}, {name}, {id},
  // {id, newName}, {id, file} and {file}, each filled from the command's
  // arguments, whose count the row's arity has already checked.
  template <typename R>
  static Run bareRequest(Describe describe = {});
  template <typename R>
  static Run nameRequest(std::string R::*name, Describe describe = {});
  template <typename R>
  static Run idRequest(std::string R::*id, Describe describe = {});
  template <typename R>
  static Run renameRequest(std::string R::*id);
  template <typename R>
  static Run saveArchiveRequest(std::string R::*id, Describe describe = {});
  template <typename R>
  static Run loadArchiveRequest(Describe describe);

  // Mints the request id, sends, and either awaits the reply (see
  // awaitReply(uint64_t)) or, under `no-wait`, parks the Describe for a
  // later await-reply.
  template <typename R>
  Failure sendRequest(R request, Deadline, Modifiers, const Describe & = {});
  // Waits for the ProjectOpReply with that id, prints it with `describe`'s
  // fields, and turns `ok` into the OK/FAIL the expect-fail prefix asks for.
  Failure awaitReply(
      uint64_t requestId, Deadline, Modifiers, const Describe &describe);
  // A Describe over one decoded Result: FAILs an ok reply that carries no
  // `name`, otherwise hands the Result to `describe(self, result, event,
  // following)`, which fills the record and the variables.
  template <typename Result, typename F>
  static Describe withResult(const char *name, F describe);
  // A Describe that reads one string id out of a Result, prints it as
  // `key=` and stores it in the named variable.
  template <typename Result>
  static Describe createdResult(
      std::string Result::*id, const char *key, const char *variable);
  // The Describe of every task-launching request: `taskId=`, $lastTaskId,
  // and the kind of message the task will complete with.
  static Describe taskStarted(TaskMessage message = TaskMessage::Other);
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
  WaitEnd pumpUntil(const std::function<bool()> &done,
      Deadline deadline,
      LossEnds lossEnds = LossEnds::Wait);
  // Polls until an event `accept`s (that event is returned in `matched`) or
  // `done` holds; an Error that `accept` does not take ends the wait as
  // WaitEnd::Error. `accept` sees each event before it is printed and may add
  // fields to it (a reply's decoded results).
  WaitEnd pumpUntilEvent(const std::function<bool(Event &)> &accept,
      Deadline deadline,
      Event *matched = nullptr,
      LossEnds lossEnds = LossEnds::Wait,
      const std::function<bool()> &done = {});
  // The FAIL reason of a wait that ended without `awaited`.
  std::string waitFailure(
      WaitEnd wait, const std::string &awaited, Deadline deadline) const;
  // The FAIL of a command that needs the link, when it is not Connected.
  Failure notConnected() const;
  // Prints whatever events are already queued without waiting; the reason
  // when one of them was an Error, which fails the command that drained it.
  Failure drainEvents();
  void printEvent(const Event &event);
  void printRecord(const std::string &line);

  TestSession *m_session{nullptr};
  std::ostream *m_out{nullptr};
  RunnerOptions m_options;

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

// Inlined definitions ////////////////////////////////////////////////////////

template <typename R>
inline CommandRunner::Run CommandRunner::bareRequest(Describe describe)
{
  return Run::Fn([describe = std::move(describe)](CommandRunner &self,
                     const Command &,
                     Deadline deadline,
                     Modifiers modifiers) {
    return self.sendRequest(R{}, deadline, modifiers, describe);
  });
}

template <typename R>
inline CommandRunner::Run CommandRunner::nameRequest(
    std::string R::*name, Describe describe)
{
  return Run::Fn([name, describe = std::move(describe)](CommandRunner &self,
                     const Command &command,
                     Deadline deadline,
                     Modifiers modifiers) {
    R request;
    if (!command.args.empty())
      request.*name = command.args[0];
    return self.sendRequest(std::move(request), deadline, modifiers, describe);
  });
}

template <typename R>
inline CommandRunner::Run CommandRunner::idRequest(
    std::string R::*id, Describe describe)
{
  return Run::Fn([id, describe = std::move(describe)](CommandRunner &self,
                     const Command &command,
                     Deadline deadline,
                     Modifiers modifiers) {
    R request;
    request.*id = command.args[0];
    return self.sendRequest(std::move(request), deadline, modifiers, describe);
  });
}

template <typename R>
inline CommandRunner::Run CommandRunner::renameRequest(std::string R::*id)
{
  return Run::Fn([id](CommandRunner &self,
                     const Command &command,
                     Deadline deadline,
                     Modifiers modifiers) {
    R request;
    request.*id = command.args[0];
    request.newName = command.args[1];
    return self.sendRequest(std::move(request), deadline, modifiers);
  });
}

template <typename R>
inline CommandRunner::Run CommandRunner::saveArchiveRequest(
    std::string R::*id, Describe describe)
{
  return Run::Fn([id, describe = std::move(describe)](CommandRunner &self,
                     const Command &command,
                     Deadline deadline,
                     Modifiers modifiers) {
    R request;
    request.*id = command.args[0];
    request.file = command.args[1];
    return self.sendRequest(std::move(request), deadline, modifiers, describe);
  });
}

template <typename R>
inline CommandRunner::Run CommandRunner::loadArchiveRequest(Describe describe)
{
  return Run::Fn([describe = std::move(describe)](CommandRunner &self,
                     const Command &command,
                     Deadline deadline,
                     Modifiers modifiers) {
    R request;
    request.file = command.args[0];
    return self.sendRequest(std::move(request), deadline, modifiers, describe);
  });
}

template <typename R>
inline CommandRunner::Failure CommandRunner::sendRequest(
    R request, Deadline deadline, Modifiers modifiers, const Describe &describe)
{
  request.requestId = m_session->nextRequestId();
  m_variables["lastRequestId"] = std::to_string(request.requestId);
  // Until the reply is collected (at once, or by await-reply under no-wait)
  // the best mark is the count as the request goes out.
  m_snapshotMark = m_session->snapshotsReceived();
  std::string error;
  if (!m_session->send(request, &error))
    return error;
  if (modifiers.noWait) {
    m_pendingReplies.emplace_back(request.requestId, describe);
    return drainEvents();
  }
  return awaitReply(request.requestId, deadline, modifiers, describe);
}

template <typename Result, typename F>
inline CommandRunner::Describe CommandRunner::withResult(
    const char *name, F describe)
{
  return [name, describe = std::move(describe)](CommandRunner &self,
             const protocol::ProjectOpReply &reply,
             Event &event,
             std::vector<Event> &following) -> Failure {
    const auto result = protocol::results<Result>(reply);
    if (!result)
      return std::string("the reply carries no ") + name;
    describe(self, *result, event, following);
    return {};
  };
}

template <typename Result>
inline CommandRunner::Describe CommandRunner::createdResult(
    std::string Result::*id, const char *key, const char *variable)
{
  return withResult<Result>(key,
      [id, key, variable](CommandRunner &self,
          const Result &result,
          Event &event,
          std::vector<Event> &) {
        event.fields.emplace_back(key, result.*id);
        self.m_variables[variable] = result.*id;
      });
}

} // namespace vsr::scivis_studio::test_client
