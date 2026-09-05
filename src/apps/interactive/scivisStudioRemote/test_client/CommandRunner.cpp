// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandRunner.h"
#include "CommandText.h"
// vsr_scivis_studio_protocol
#include "ShotRigRequests.h"
// std
#include <algorithm>
#include <ostream>

namespace vsr::scivis_studio::test_client {

using namespace protocol;

// Handlers as table rows /////////////////////////////////////////////////////

CommandRunner::Run::Run(Failure (CommandRunner::*handler)(const Command &))
    : fn([handler](
             CommandRunner &self, const Command &command, Deadline, Modifiers) {
        return (self.*handler)(command);
      })
{}

CommandRunner::Run::Run(
    Failure (CommandRunner::*handler)(const Command &, Deadline))
    : fn([handler](CommandRunner &self,
             const Command &command,
             Deadline deadline,
             Modifiers) { return (self.*handler)(command, deadline); })
{}

CommandRunner::Run::Run(
    Failure (CommandRunner::*handler)(const Command &, Deadline, Modifiers))
    : fn([handler](CommandRunner &self,
             const Command &command,
             Deadline deadline,
             Modifiers modifiers) {
        return (self.*handler)(command, deadline, modifiers);
      })
{}

CommandRunner::Run::Run(Fn f) : fn(std::move(f)) {}

// The command table ///////////////////////////////////////////////////////////

const std::vector<CommandRunner::CommandSpec> &CommandRunner::commands()
{
  using K = Kind;
  static const Describe lightRigCreated = createdResult<LightRigCreatedResult>(
      &LightRigCreatedResult::lightRigId, "lightRigId", "lastLightRigId");
  static const Describe cameraRigCreated =
      createdResult<CameraRigCreatedResult>(
          &CameraRigCreatedResult::cameraRigId,
          "cameraRigId",
          "lastCameraRigId");
  static const Describe shotCreated = createdResult<ShotCreatedResult>(
      &ShotCreatedResult::shotId, "shotId", "lastShotId");

  // Sorted by name; findCommand() searches it as such. A row's usage is what
  // usageError(), --help and the README print; its arity is what execute()
  // checks. `sync` and `task` in a request's summary say whether the reply
  // ends the op or launches a Server Task.
  // clang-format off
  static const std::vector<CommandSpec> table = {
      {"add-light", "<lightRigId> [subtype]", 1, 2, K::Request,
          &CommandRunner::addLight,
          "sync; adds a light to the rig (SUBTYPE defaults to directional);"
          " the reply carries lightNode=LAYER:NODE"},
      {"assert", "<value> <op> <rhs>", 3, 3, K::Session,
          &CommandRunner::assertValue,
          "compare a named value (the assert values below); OP in"
          " == != < <= > >= contains; RHS a literal, or @NAME for another"
          " named value"},
      {"await-frame", "[count]", 0, 1, K::Session,
          &CommandRunner::awaitFrame,
          "wait for COUNT (default 1) further frames"},
      {"await-frame-advance", "[count]", 0, 1, K::Session,
          &CommandRunner::awaitFrameAdvance,
          "wait for COUNT (default 1) further frames whose header frame"
          " differs from the previous frame's"},
      {"await-frame-at", "<frame>", 1, 1, K::Session,
          &CommandRunner::awaitFrameAt,
          "wait for a Frame whose header says frame == FRAME; the headers"
          " meanwhile print as they come"},
      {"await-lost", "", 0, 0, K::Session,
          &CommandRunner::awaitLost,
          "wait until the connection is Lost (mirror and replica stay as a"
          " frozen view)"},
      {"await-reply", "[requestId]", 0, 1, K::Wait,
          &CommandRunner::awaitReply,
          "collect the reply of a no-wait request, the oldest pending by"
          " default"},
      {"await-snapshot", "", 0, 0, K::Session,
          &CommandRunner::awaitSnapshot,
          "wait for a ProjectSnapshot newer than the last thing awaited (the"
          " last request's reply, the last await-task's end, or the previous"
          " await-snapshot); each await consumes one snapshot"},
      {"await-task", "[taskId]", 0, 1, K::Wait,
          &CommandRunner::awaitTask,
          "wait for the task (default $lastTaskId) to end; FAIL on"
          " TaskFailed, or under expect-fail on TaskCompleted"},
      {"await-task-progress", "[taskId]", 0, 1, K::Session,
          &CommandRunner::awaitTaskProgress,
          "wait until the task (default $lastTaskId) has reported progress at"
          " least once (a report an earlier command printed counts); FAIL at"
          " once when it ends without any"},
      {"await-warning", "", 0, 0, K::Session,
          &CommandRunner::awaitWarning,
          "wait for the next TimeAdvanceWarning (a frame that failed to load"
          " while playing)"},
      {"cancel-task", "<taskId>", 1, 1, K::Request,
          &CommandRunner::cancelTask,
          "sync; removes a queued task (it then ends as TaskFailed"
          " \"cancelled\") or asks a running render to stop at its next frame;"
          " a finished task is an error reply"},
      {"clone-light-rig", "<id>", 1, 1, K::Request,
          idRequest<CloneLightRig>(&CloneLightRig::lightRigId, lightRigCreated),
          "sync; lightRigId="},
      {"connect", "[host] [port]", 0, 2, K::Session,
          &CommandRunner::connect,
          "TCP connect, exchange Hellos (exact PROTOCOL_VERSION match), await"
          " the complete Bootstrap"},
      {"create-camera-rig", "[name]", 0, 1, K::Request,
          nameRequest<CreateCameraRig>(
              &CreateCameraRig::name, cameraRigCreated),
          "sync; cameraRigId="},
      {"create-color-map", "[name]", 0, 1, K::Request,
          &CommandRunner::createColorMap,
          "sync; colorMapId= and object=TYPE:INDEX, the record and its"
          " scene-side Array"},
      {"create-light-rig", "[name]", 0, 1, K::Request,
          nameRequest<CreateLightRig>(&CreateLightRig::name, lightRigCreated),
          "sync; lightRigId="},
      {"create-shot", "[name]", 0, 1, K::Request,
          nameRequest<CreateShot>(&CreateShot::name, shotCreated),
          "sync; shotId=; the new shot becomes active"},
      {"declare-file-animation-dataset",
          "<name> <importer> <source>... [set-frame-count=BOOL]", 3, -1,
          K::Request, &CommandRunner::declareFileAnimationDataset,
          "sync; declares a file animation without importing it; the reply"
          " carries datasetId="},
      {"disconnect", "", 0, 0, K::Session,
          &CommandRunner::disconnect,
          "send Disconnect and close; mirror and replica are cleared ->"
          " Disconnected"},
      {"discover-dataset-candidates", "", 0, 0, K::Request,
          &CommandRunner::discoverDatasetCandidates,
          "sync; one EVT DatasetCandidate file= proposedName= per candidate"},
      {"dump-frame", "", 0, 0, K::Session,
          &CommandRunner::dumpFrame,
          "one EVT Frame line for the newest frame header"},
      {"dump-layers", "", 0, 0, K::Session,
          &CommandRunner::dumpLayers,
          "one EVT Layer line per mirror layer: index, name, nodes, active"},
      {"dump-project", "", 0, 0, K::Session,
          &CommandRunner::dumpProject,
          "one EVT Project line from the replica (name, activeShot, counts,"
          " dirty, directory), then one EVT Shot, Dataset, LightRig, CameraRig"
          " and ColorMap line per entity"},
      {"dump-scene", "", 0, 0, K::Session,
          &CommandRunner::dumpScene,
          "one EVT Object line per mirror object: type, index, subtype, name,"
          " params count"},
      {"dump-ui-state", "", 0, 0, K::Session,
          &CommandRunner::dumpUIState,
          "one EVT UIState present= children= line for the newest tree the"
          " server sent, then one EVT UIStateEntry path= value= per leaf"},
      {"expect-error", "[substring]", 0, 1, K::Session,
          &CommandRunner::expectError,
          "the next server message other than a Frame or a liveness Pong must"
          " be an Error, containing SUBSTRING if given"},
      {"expect-pong", "", 0, 0, K::Session,
          &CommandRunner::expectPong,
          "the next non-Frame server message must be a Pong"},
      {"find-object", "<type> [first|name=<name>]", 1, 2, K::Session,
          &CommandRunner::findObject,
          "the first mirror object of that type, or the first one so named:"
          " one EVT Object line, and $lastObjectRef (type:index),"
          " $lastObjectType, $lastObjectIndex"},
      {"import-file-animation-dataset",
          "<name> <importer> <path>... [set-frame-count=BOOL]", 3, -1,
          K::Request, &CommandRunner::importFileAnimationDataset,
          "task: import a file animation from those files"},
      {"import-static-dataset", "<path> [name] [importer|VSR_SUBTREE]", 1, 3,
          K::Request, &CommandRunner::importStaticDataset,
          "task; IMPORTER is a vsr::io::ImporterType name (OBJ, PLY, ...) or"
          " VSR_SUBTREE for a subtree archive"},
      {"incorporate-dataset-candidate", "<file> [proposedName] [name]", 1, 3,
          K::Request, &CommandRunner::incorporateDatasetCandidate,
          "task: import a candidate discover-dataset-candidates found"},
      {"list-directory", "<directory>", 1, 1, K::Request,
          &CommandRunner::listDirectory,
          "one EVT DirectoryEntry name= kind= size= mtime= per entry (File,"
          " Directory, ProjectDirectory); refused outside every Data Root"},
      {"list-roots", "", 0, 0, K::Request,
          &CommandRunner::listRoots,
          "one EVT DataRoot path= per Data Root; the first is $dataRoot"},
      {"load-camera-rig-archive", "<file>", 1, 1, K::Request,
          loadArchiveRequest<LoadCameraRigArchive>(cameraRigCreated),
          "sync; cameraRigId="},
      {"load-dataset", "<id>", 1, 1, K::Request,
          idRequest<LoadDataset>(&LoadDataset::datasetId, taskStarted()),
          "task: load an unloaded dataset"},
      {"load-dataset-archive", "<file>", 1, 1, K::Request,
          loadArchiveRequest<LoadDatasetArchive>(
              taskStarted(TaskMessage::DatasetId)),
          "task: import a Dataset Archive"},
      {"load-light-rig-archive", "<file>", 1, 1, K::Request,
          loadArchiveRequest<LoadLightRigArchive>(lightRigCreated),
          "sync; lightRigId="},
      {"new-project", "", 0, 0, K::Request,
          bareRequest<NewProject>(),
          "sync: an unsaved empty project replaces the current one"},
      {"open-project", "<directory>", 1, 1, K::Request,
          &CommandRunner::openProject,
          "task: open the project stored in DIR"},
      {"pick", "<x> <y>", 2, 2, K::Session,
          &CommandRunner::pick,
          "send a Pick at that pixel (x right, y down from the top-left) and"
          " await its PickReply; sets $lastPickType and $lastPickIndex on a"
          " hit, unsets them on a miss"},
      {"ping", "", 0, 0, K::Session,
          &CommandRunner::ping,
          "send Ping; the Pong is for expect-pong"},
      {"reconnect", "", 0, 0, K::Session,
          &CommandRunner::reconnect,
          "connect again to the last host and port, retrying refused attempts"
          " until the deadline; a fresh handshake and Bootstrap"},
      {"refresh-dataset-availability", "<id>", 1, 1, K::Request,
          idRequest<RefreshDatasetAvailability>(
              &RefreshDatasetAvailability::datasetId),
          "sync; checks again whether the dataset's source is there"},
      {"reimport-dataset", "<id>", 1, 1, K::Request,
          idRequest<ReimportDataset>(
              &ReimportDataset::datasetId, taskStarted()),
          "task: import the dataset again from its source"},
      {"remove-camera-rig", "<id>", 1, 1, K::Request,
          idRequest<RemoveCameraRig>(&RemoveCameraRig::cameraRigId),
          "sync"},
      {"remove-color-map", "<id>", 1, 1, K::Request,
          idRequest<RemoveColorMap>(&RemoveColorMap::colorMapId),
          "sync"},
      {"remove-dataset", "<id> [keep-asset-file]", 1, 2, K::Request,
          &CommandRunner::removeDataset,
          "sync; removes the saved asset file too unless told to keep it"},
      {"remove-light", "<lightRigId> <layer> <nodeIndex>", 3, 3, K::Request,
          &CommandRunner::removeLight,
          "sync; the node reference add-light returned"},
      {"remove-light-rig", "<id>", 1, 1, K::Request,
          idRequest<RemoveLightRig>(&RemoveLightRig::lightRigId),
          "sync"},
      {"remove-param", "<type> <index> <name>", 3, 3, K::Session,
          &CommandRunner::removeParam,
          "remove a parameter, mirror and wire"},
      {"remove-shot", "<id>", 1, 1, K::Request,
          idRequest<RemoveShot>(&RemoveShot::shotId),
          "sync"},
      {"rename-camera-rig", "<id> <newName>", 2, 2, K::Request,
          renameRequest<RenameCameraRig>(&RenameCameraRig::cameraRigId),
          "sync"},
      {"rename-color-map", "<id> <newName>", 2, 2, K::Request,
          renameRequest<RenameColorMap>(&RenameColorMap::colorMapId),
          "sync"},
      {"rename-dataset", "<id> <newName>", 2, 2, K::Request,
          renameRequest<RenameDataset>(&RenameDataset::datasetId),
          "sync"},
      {"rename-light-rig", "<id> <newName>", 2, 2, K::Request,
          renameRequest<RenameLightRig>(&RenameLightRig::lightRigId),
          "sync"},
      {"render-shot", "<shotId|active>", 1, 1, K::Request,
          &CommandRunner::renderShot,
          "task: render the shot's frames offline; needs a saved project,"
          " refused while another render is queued or running; the end carries"
          " framesCompleted= and the output directory as its message"},
      {"request-array-histogram",
          "<type> <index> <bins> (or <type:index> <bins>)", 2, 3, K::Request,
          &CommandRunner::requestArrayHistogram,
          "sync: bin a scalar array on the server; the reply prints bins= min="
          " max= nonFinite= and fills the histogram.* values (cleared when the"
          " request goes out)"},
      {"save-camera-rig-archive", "<id> <file>", 2, 2, K::Request,
          saveArchiveRequest<SaveCameraRigArchive>(
              &SaveCameraRigArchive::cameraRigId),
          "sync"},
      {"save-dataset-archive", "<id> <file>", 2, 2, K::Request,
          saveArchiveRequest<SaveDatasetArchive>(
              &SaveDatasetArchive::datasetId, taskStarted()),
          "task: write the dataset as a Dataset Archive"},
      {"save-frame", "<path.ppm>", 1, 1, K::Session,
          &CommandRunner::saveFrame,
          "decode the newest frame into a binary P6 PPM (relative to the"
          " working directory)"},
      {"save-light-rig-archive", "<id> <file>", 2, 2, K::Request,
          saveArchiveRequest<SaveLightRigArchive>(
              &SaveLightRigArchive::lightRigId),
          "sync"},
      {"save-project", "[directory]", 0, 1, K::Request,
          &CommandRunner::saveProject,
          "task: save to DIR, or to the project's own directory; sends the UI"
          " state tree set-ui-state built, if any"},
      {"send-raw", "<typeByte 0..255> [hex bytes...]", 1, -1, K::Session,
          &CommandRunner::sendRaw,
          "send a message of that type byte with the given payload bytes,"
          " verbatim"},
      {"set-active-shot", "<id>", 1, 1, K::Request,
          idRequest<SetActiveShot>(&SetActiveShot::shotId),
          "sync"},
      {"set-encodings", "<name>[,<name>...]", 1, 1, K::Session,
          &CommandRunner::setEncodings,
          "offer frame encodings, most preferred first (raw, turbojpeg;"
          " case-insensitive)"},
      {"set-frame-config", "<width> <height>", 2, 2, K::Session,
          &CommandRunner::setFrameConfig,
          "request a frame size, await the FrameConfig ack"},
      {"set-node-transform", "<layer> <node> <16 floats>", 18, 18, K::Session,
          &CommandRunner::setNodeTransform,
          "set a transform node's matrix, column-major; NODE is the server's"
          " node index"},
      {"set-outline", "[<type> <index> | <type:index> | none]", 0, 2,
          K::Session, &CommandRunner::setOutline,
          "outline that object, or clear the outline (none or no argument)"},
      {"set-param", "<type> <index> <name> <anariType> <value...>", 5, -1,
          K::Session, &CommandRunner::setParam,
          "optimistic edit: set a parameter on the mirror and on the wire"
          " (camera 1 fovy float32 0.9)"},
      {"set-playing", "<shotId|active> on|off", 2, 2, K::Request,
          &CommandRunner::setPlaying,
          "sync: start or stop playback of the active shot (another id is"
          " refused); the reply and a snapshot follow"},
      {"set-time", "<shotId|active> <frame>", 2, 2, K::Session,
          &CommandRunner::setTime,
          "one-way scrub (latest-wins); while paused the server commits Time"
          " at Rest with one debounced snapshot; a SHOT that is not active is"
          " ignored silently"},
      {"set-ui-state", "<key>=<value>... | none", 1, -1, K::Session,
          &CommandRunner::setUIState,
          "build the UI state tree the next save-projects send, one string"
          " leaf windows/<key> per edit (repeated commands compose); none"
          " drops the tree"},
      {"shutdown", "", 0, 0, K::Session,
          &CommandRunner::shutdown,
          "send Shutdown and await the server closing the socket ->"
          " Disconnected"},
      {"sleep", "<ms>", 1, 1, K::Session,
          &CommandRunner::sleep,
          "keep polling (events still print) for MS milliseconds; a loss"
          " meanwhile is the next command's to notice"},
      {"start-rendering", "", 0, 0, K::Session,
          &CommandRunner::startRendering,
          "ask the server to stream frames"},
      {"stop-rendering", "", 0, 0, K::Session,
          &CommandRunner::stopRendering,
          "pause the stream"},
      {"unload-dataset", "<id>", 1, 1, K::Request,
          idRequest<UnloadDataset>(&UnloadDataset::datasetId),
          "sync"},
      {"update-shot", "<id> <field>=<value>...", 2, -1, K::Request,
          &CommandRunner::updateShot,
          "sync: the replica's Shot with the edits applied is sent whole;"
          " fields name, frameCount, fps, loop, currentFrame, lightRigId,"
          " cameraRigId, renderSettings.*, binding.<datasetId>=on|off"},
      {"viewport-settings", "<key>=<value>...", 0, -1, K::Session,
          &CommandRunner::viewportSettings,
          "edit the remembered ViewportSettings and send the whole struct"
          " (unset keys keep their last value); keys highlightSelection,"
          " outlinePrimitives, showWorldBounds, edgeInvert,"
          " worldBoundsColor=r,g,b,a, worldBoundsWidth, visualizeAOV,"
          " depthVisualMinimum, depthVisualMaximum"},
  };
  // clang-format on
  return table;
}

const CommandRunner::CommandSpec *CommandRunner::findCommand(
    const std::string &name)
{
  const auto &table = commands();
  const auto it = std::lower_bound(table.begin(),
      table.end(),
      name,
      [](const CommandSpec &spec, const std::string &n) {
        return n.compare(spec.name) > 0;
      });
  if (it == table.end() || name != it->name)
    return nullptr;
  return &*it;
}

// Construction ///////////////////////////////////////////////////////////////

CommandRunner::CommandRunner(
    TestSession *session, std::ostream *out, RunnerOptions options)
    : m_session(session), m_out(out), m_options(std::move(options))
{}

// Running ////////////////////////////////////////////////////////////////////

bool CommandRunner::run(const std::vector<Command> &commands)
{
  bool ok = true;
  for (const auto &command : commands) {
    if (!runCommand(command)) {
      ok = false;
      if (!m_options.keepGoing)
        break;
    }
  }
  drainEvents(); // nothing is in flight any more for an Error to fail
  return ok;
}

bool CommandRunner::runCommand(const Command &command)
{
  const auto failure = execute(command);
  if (failure) {
    printRecord("FAIL " + command.text + ": " + *failure);
    return false;
  }
  printRecord("OK " + command.text);
  return true;
}

CommandRunner::Failure CommandRunner::execute(Command command)
{
  Modifiers modifiers;
  while (command.name == "expect-fail" || command.name == "no-wait") {
    if (command.args.empty())
      return "usage: " + command.name + " <request command> [args...]";
    (command.name == "expect-fail" ? modifiers.expectFail : modifiers.noWait) =
        true;
    command.name = command.args.front();
    command.args.erase(command.args.begin());
  }

  std::string error;
  if (!expandVariables(
          command,
          [this](const std::string &n) { return variable(n); },
          &error))
    return error;
  const auto suffix = takeTimeoutSuffix(command, &error);
  if (!error.empty())
    return error;
  const Deadline deadline = suffix ? *suffix : m_options.timeout;

  const auto *spec = findCommand(command.name);
  if (!spec)
    return "unknown command '" + command.name + "'";
  if (modifiers.noWait && spec->kind != Kind::Request)
    return "no-wait applies to request commands, not to " + command.name;
  if (modifiers.expectFail && spec->kind == Kind::Session) {
    return "expect-fail applies to request commands and waits, not to "
        + command.name;
  }
  const int argc = int(command.args.size());
  if (argc < spec->minArgs || (spec->maxArgs >= 0 && argc > spec->maxArgs))
    return usageError(command);
  return spec->run.fn(*this, command, deadline, modifiers);
}

std::string CommandRunner::usageError(const Command &command)
{
  const auto *spec = findCommand(command.name);
  const std::string usage = spec ? spec->usage : "";
  return "usage: " + command.name + (usage.empty() ? "" : " " + usage);
}

// Variables and ids //////////////////////////////////////////////////////////

std::optional<std::string> CommandRunner::variable(
    const std::string &name) const
{
  const auto *value = m_variables.at(name);
  if (!value)
    return {};
  return *value;
}

// Pumping and output /////////////////////////////////////////////////////////

CommandRunner::WaitEnd CommandRunner::pumpUntil(
    const std::function<bool()> &done, Deadline deadline, LossEnds lossEnds)
{
  // Every event is taken as one, so the `done` test and the Error and Lost
  // checks see the same picture.
  return pumpUntilEvent(
      [&](const Event &) { return false; }, deadline, nullptr, lossEnds, done);
}

CommandRunner::WaitEnd CommandRunner::pumpUntilEvent(
    const std::function<bool(Event &)> &accept,
    Deadline deadline,
    Event *matched,
    LossEnds lossEnds,
    const std::function<bool()> &done)
{
  // A wait that starts in Lost is not ended by it (await-lost, sleep after a
  // loss); one that watches the link go is.
  const bool wasLost = m_session->state() == SessionState::Lost;
  WaitEnd wait = WaitEnd::TimedOut;
  m_session->pollUntil(
      [&] {
        Event event;
        while (m_session->takeEvent(event)) {
          // accept() first: it may add the fields the record then shows.
          const bool accepted = accept(event);
          printEvent(event);
          if (accepted) {
            if (matched)
              *matched = std::move(event);
            wait = WaitEnd::Done;
            return true;
          }
          if (event.name == "Error") {
            // Not what this command waited for: the server is objecting to
            // something, and the rest of the queue is the next command's.
            wait = WaitEnd::Error;
            return true;
          }
        }
        if (done && done()) {
          wait = WaitEnd::Done;
          return true;
        }
        if (lossEnds == LossEnds::Wait && !wasLost
            && m_session->state() == SessionState::Lost) {
          wait = WaitEnd::Lost;
          return true;
        }
        return false;
      },
      deadline);
  return wait;
}

std::string CommandRunner::waitFailure(
    WaitEnd wait, const std::string &awaited, Deadline deadline) const
{
  switch (wait) {
  case WaitEnd::Lost:
    return "connection lost while waiting for " + awaited + ": "
        + m_session->failure();
  case WaitEnd::Error:
    return "server answered Error " + quoted(m_session->lastError())
        + " while waiting for " + awaited;
  case WaitEnd::TimedOut:
  case WaitEnd::Done:
    break;
  }
  return "no " + awaited + " within " + std::to_string(deadline.count())
      + " ms";
}

CommandRunner::Failure CommandRunner::notConnected() const
{
  if (m_session->state() == SessionState::Connected)
    return {};
  return std::string("not connected (") + toString(m_session->state()) + ")";
}

CommandRunner::Failure CommandRunner::drainEvents()
{
  m_session->poll();
  Event event;
  Failure failure;
  while (m_session->takeEvent(event)) {
    printEvent(event);
    if (event.name == "Error" && !failure && !event.fields.empty())
      failure = "server answered Error " + event.fields.front().second;
  }
  return failure;
}

void CommandRunner::printEvent(const Event &event)
{
  if (m_options.quietEvents)
    return;
  printRecord("EVT " + event.text());
}

void CommandRunner::printRecord(const std::string &line)
{
  if (m_out)
    *m_out << line << '\n' << std::flush;
}

} // namespace vsr::scivis_studio::test_client
