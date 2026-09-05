// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/*
 * The CommandRunner request commands with arguments of their own: the
 * Project Ops, Remote Browse and task requests that a request shape in the
 * table (idRequest<R> and friends, CommandRunner.h) cannot express, plus the
 * Describe every task-launching request shares. Each builds one request and
 * hands it to sendRequest(), which mints the id, sends and awaits the reply.
 * The table in CommandRunner.cpp has checked each command's argument count
 * before a handler runs.
 */

#include "AnyText.h"
#include "CommandRunner.h"
#include "CommandText.h"
// vsr_scivis_studio_protocol
#include "ShotRigRequests.h"
#include "TaskMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
#include "Shot.h"
// std
#include <limits>

namespace vsr::scivis_studio::test_client {

using namespace protocol;

namespace {

// The wire names of vsr::io::ImporterType ("OBJ", "VOLUME_ANIMATION", ...),
// case-insensitive.
std::optional<vsr::io::ImporterType> parseImporter(const std::string &text)
{
  return importerTypeFromString(upper(text));
}

// A trailing `key=value` argument, split off when present.
std::optional<std::string> takeOption(
    std::vector<std::string> &args, const char *key)
{
  if (args.empty())
    return {};
  const std::string prefix = std::string(key) + "=";
  if (args.back().compare(0, prefix.size(), prefix) != 0)
    return {};
  auto value = args.back().substr(prefix.size());
  args.pop_back();
  return value;
}

// Applies one `field=value` edit of update-shot to a Shot; false with the
// reason on an unknown field or a value the field cannot hold.
bool applyShotField(Shot &shot,
    const std::string &field,
    const std::string &value,
    std::string &error)
{
  const auto badValue = [&] {
    error = "not a valid " + field + ": " + value;
    return false;
  };
  long long integer = 0;
  unsigned long long natural = 0;
  double number = 0;
  bool flag = false;
  auto &rs = shot.renderSettings;

  if (field == "name")
    shot.name = value;
  else if (field == "frameCount") {
    if (!parseInteger(value, integer) || integer < 1
        || integer > std::numeric_limits<int>::max())
      return badValue();
    shot.frameCount = int(integer);
  } else if (field == "fps") {
    if (!parseDouble(value, number) || number <= 0)
      return badValue();
    shot.fps = float(number);
  } else if (field == "currentFrame") {
    if (!parseInteger(value, integer) || integer < 0
        || integer > std::numeric_limits<int>::max())
      return badValue();
    shot.currentFrame = int(integer);
  } else if (field == "loop") {
    if (!parseBool(value, flag))
      return badValue();
    shot.loop = flag;
  } else if (field == "playing") {
    error = "playing is playback state (SetPlaying), not a Shot edit";
    return false;
  } else if (field == "lightRigId")
    shot.lightRigId = value;
  else if (field == "cameraRigId")
    shot.cameraRigId = value;
  else if (field == "renderSettings.width" || field == "renderSettings.height"
      || field == "renderSettings.samples") {
    if (!parseNonNegative(value, natural) || natural < 1
        || natural > std::numeric_limits<uint32_t>::max())
      return badValue();
    (field == "renderSettings.width"           ? rs.width
            : field == "renderSettings.height" ? rs.height
                                               : rs.samples) =
        uint32_t(natural);
  } else if (field == "renderSettings.rendererLibrary")
    rs.rendererLibrary = value;
  else if (field == "renderSettings.rendererSubtype")
    rs.rendererSubtype = value;
  else if (field == "renderSettings.outputFilePrefix")
    rs.outputFilePrefix = value;
  else if (field == "renderSettings.rendererObjectIndex") {
    if (value == "none")
      rs.rendererObjectIndex = VSR_INVALID_INDEX;
    else if (parseNonNegative(value, natural))
      rs.rendererObjectIndex = size_t(natural);
    else
      return badValue();
  } else if (field.rfind("binding.", 0) == 0 && field.size() > 8) {
    if (!parseBool(value, flag))
      return badValue();
    shot::setDatasetBinding(shot, field.substr(8), flag);
  } else {
    error = "unknown Shot field '" + field
        + "'; valid: name, frameCount, fps, currentFrame, loop, lightRigId,"
          " cameraRigId, renderSettings.{width,height,samples,rendererLibrary,"
          "rendererSubtype,rendererObjectIndex,outputFilePrefix},"
          " binding.<datasetId>";
    return false;
  }
  return true;
}

} // namespace

// Project ////////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::openProject(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  OpenProject request;
  request.directory = command.args[0];
  return sendRequest(std::move(request), deadline, modifiers, taskStarted());
}

CommandRunner::Failure CommandRunner::saveProject(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  SaveProject request;
  if (!command.args.empty())
    request.directory = std::filesystem::path(command.args[0]);
  // The GUI sends its live layout with every save; this client sends the
  // tree set-ui-state built, or none, when the server keeps the one the
  // project opened with.
  request.uiState = m_uiStateToSave;
  return sendRequest(std::move(request), deadline, modifiers, taskStarted());
}

// Datasets ///////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::importStaticDataset(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  const std::string name = command.args.size() > 1 ? command.args[1] : "";
  if (command.args.size() > 2 && upper(command.args[2]) == "VSR_SUBTREE") {
    ImportSubtreeDataset request;
    request.sourcePath = command.args[0];
    request.name = name;
    return sendRequest(std::move(request),
        deadline,
        modifiers,
        taskStarted(TaskMessage::DatasetId));
  }
  ImportStaticDataset request;
  request.sourcePath = command.args[0];
  request.name = name;
  if (command.args.size() > 2) {
    const auto importer = parseImporter(command.args[2]);
    if (!importer)
      return "unknown importer '" + command.args[2] + "'";
    request.importerType = *importer;
  }
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      taskStarted(TaskMessage::DatasetId));
}

CommandRunner::Failure CommandRunner::importFileAnimationDataset(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  auto args = command.args;
  const auto frameCount = takeOption(args, "set-frame-count");
  // The option counted towards the arity the table checked.
  if (args.size() < 3)
    return usageError(command);
  ImportFileAnimationDataset request;
  request.name = args[0];
  const auto importer = parseImporter(args[1]);
  if (!importer)
    return "unknown importer '" + args[1] + "'";
  request.importerType = *importer;
  for (size_t i = 2; i < args.size(); ++i)
    request.sourcePaths.emplace_back(args[i]);
  if (frameCount && !parseBool(*frameCount, request.setActiveShotFrameCount))
    return "set-frame-count must be true or false, got: " + *frameCount;
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      taskStarted(TaskMessage::DatasetId));
}

CommandRunner::Failure CommandRunner::declareFileAnimationDataset(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  auto args = command.args;
  const auto frameCount = takeOption(args, "set-frame-count");
  if (args.size() < 3)
    return usageError(command);
  DeclareFileAnimationDataset request;
  request.name = args[0];
  const auto importer = parseImporter(args[1]);
  if (!importer)
    return "unknown importer '" + args[1] + "'";
  request.importerType = *importer;
  request.sourceList.assign(args.begin() + 2, args.end());
  if (frameCount && !parseBool(*frameCount, request.setActiveShotFrameCount))
    return "set-frame-count must be true or false, got: " + *frameCount;
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      createdResult<DatasetCreatedResult>(
          &DatasetCreatedResult::datasetId, "datasetId", "lastDatasetId"));
}

CommandRunner::Failure CommandRunner::removeDataset(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  if (command.args.size() == 2 && command.args[1] != "keep-asset-file")
    return usageError(command);
  RemoveDataset request;
  request.datasetId = command.args[0];
  request.keepAssetFile = command.args.size() == 2;
  return sendRequest(std::move(request), deadline, modifiers);
}

CommandRunner::Failure CommandRunner::discoverDatasetCandidates(
    const Command &, Deadline deadline, Modifiers modifiers)
{
  return sendRequest(DiscoverDatasetCandidates{},
      deadline,
      modifiers,
      [](CommandRunner &,
          const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &following) -> Failure {
        const auto result = results<DiscoverDatasetCandidatesResult>(reply);
        if (!result)
          return "the reply carries no DiscoverDatasetCandidatesResult";
        event.fields.emplace_back(
            "candidates", std::to_string(result->candidates.size()));
        for (const auto &candidate : result->candidates) {
          Event entry{"DatasetCandidate", {}};
          entry.fields.emplace_back(
              "file", quoted(candidate.file.generic_string()));
          entry.fields.emplace_back(
              "proposedName", quoted(candidate.proposedName));
          following.push_back(std::move(entry));
        }
        return {};
      });
}

CommandRunner::Failure CommandRunner::incorporateDatasetCandidate(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  IncorporateDatasetCandidate request;
  request.file = command.args[0];
  if (command.args.size() > 1)
    request.proposedName = command.args[1];
  if (command.args.size() > 2)
    request.name = command.args[2];
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      taskStarted(TaskMessage::DatasetId));
}

// Shots and rigs /////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::updateShot(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  std::string error;
  const auto *current = replicaShot(command.args[0], error);
  if (!current)
    return error;
  UpdateShot request;
  request.shot = *current;
  for (size_t i = 1; i < command.args.size(); ++i) {
    const auto &edit = command.args[i];
    const auto eq = edit.find('=');
    if (eq == std::string::npos || eq == 0)
      return "not a <field>=<value> edit: " + edit;
    if (!applyShotField(
            request.shot, edit.substr(0, eq), edit.substr(eq + 1), error))
      return error;
  }
  return sendRequest(std::move(request), deadline, modifiers);
}

CommandRunner::Failure CommandRunner::setPlaying(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  bool playing = false;
  if (!parseBool(command.args[1], playing))
    return usageError(command);
  std::string error;
  const auto shotId = shotIdArgument(command.args[0], error);
  if (!shotId)
    return error;
  SetPlaying request;
  request.shotId = *shotId;
  request.playing = playing;
  return sendRequest(std::move(request), deadline, modifiers);
}

CommandRunner::Failure CommandRunner::requestArrayHistogram(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  RequestArrayHistogram request;
  size_t consumed = 0;
  std::string error;
  unsigned long long bins = 0;
  if (!parseObjectRefArgs(command.args, 0, request.array, consumed, error)
      || command.args.size() != consumed + 1
      || !parseNonNegative(command.args[consumed], bins)
      || bins > std::numeric_limits<uint32_t>::max())
    return usageError(command);
  request.binCount = uint32_t(bins);
  m_histogram.reset();
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      [](CommandRunner &self,
          const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &) -> Failure {
        const auto result = results<ArrayHistogramResult>(reply);
        if (!result)
          return "the reply carries no ArrayHistogramResult";
        event.fields.emplace_back("bins", std::to_string(result->bins.size()));
        event.fields.emplace_back("min", numberText(result->minValue));
        event.fields.emplace_back("max", numberText(result->maxValue));
        event.fields.emplace_back(
            "nonFinite", std::to_string(result->nonFinite));
        self.m_histogram = *result;
        return {};
      });
}

CommandRunner::Failure CommandRunner::addLight(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  AddLightToRig request;
  request.lightRigId = command.args[0];
  if (command.args.size() > 1)
    request.subtype = command.args[1];
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      [](CommandRunner &self,
          const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &) -> Failure {
        const auto result = results<LightAddedResult>(reply);
        if (!result)
          return "the reply carries no LightAddedResult";
        event.fields.emplace_back("lightNode", nodeText(result->lightNode));
        self.m_variables["lastLightLayer"] = result->lightNode.layerName;
        self.m_variables["lastLightNode"] =
            std::to_string(result->lightNode.nodeIndex);
        return {};
      });
}

CommandRunner::Failure CommandRunner::removeLight(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  unsigned long long index = 0;
  if (!parseNonNegative(command.args[2], index))
    return usageError(command);
  RemoveLightFromRig request;
  request.lightRigId = command.args[0];
  request.lightNode.layerName = command.args[1];
  request.lightNode.nodeIndex = size_t(index);
  return sendRequest(std::move(request), deadline, modifiers);
}

CommandRunner::Failure CommandRunner::createColorMap(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  CreateColorMap request;
  if (!command.args.empty())
    request.name = command.args[0];
  // The reply names the record and its scene-side Array, so both the id and
  // the object variables are filled.
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      [](CommandRunner &self,
          const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &) -> Failure {
        const auto result = results<ColorMapCreatedResult>(reply);
        if (!result)
          return "the reply carries no ColorMapCreatedResult";
        event.fields.emplace_back("colorMapId", result->colorMapId);
        event.fields.emplace_back("object", objectRefText(result->object));
        self.m_variables["lastColorMapId"] = result->colorMapId;
        self.m_variables["lastObjectRef"] = objectRefText(result->object);
        self.m_variables["lastObjectType"] = shortTypeName(result->object.type);
        self.m_variables["lastObjectIndex"] =
            std::to_string(result->object.objectIndex);
        return {};
      });
}

// Remote Browse //////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::listRoots(
    const Command &, Deadline deadline, Modifiers modifiers)
{
  return sendRequest(ListRoots{},
      deadline,
      modifiers,
      [](CommandRunner &self,
          const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &following) -> Failure {
        const auto result = results<ListRootsResult>(reply);
        if (!result)
          return "the reply carries no ListRootsResult";
        event.fields.emplace_back(
            "roots", std::to_string(result->roots.size()));
        for (const auto &root : result->roots) {
          Event entry{"DataRoot", {}};
          entry.fields.emplace_back("path", quoted(root.generic_string()));
          following.push_back(std::move(entry));
        }
        if (!result->roots.empty())
          self.m_variables["dataRoot"] = result->roots.front().generic_string();
        return {};
      });
}

CommandRunner::Failure CommandRunner::listDirectory(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  ListDirectory request;
  request.directory = command.args[0];
  m_browseEntries.clear();
  return sendRequest(std::move(request),
      deadline,
      modifiers,
      [](CommandRunner &self,
          const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &following) -> Failure {
        const auto result = results<ListDirectoryResult>(reply);
        if (!result)
          return "the reply carries no ListDirectoryResult";
        self.m_browseEntries = result->entries;
        event.fields.emplace_back(
            "entries", std::to_string(result->entries.size()));
        for (const auto &e : result->entries) {
          Event entry{"DirectoryEntry", {}};
          entry.fields.emplace_back("name", quoted(e.name));
          entry.fields.emplace_back("kind", toString(e.kind));
          entry.fields.emplace_back("size", std::to_string(e.size));
          entry.fields.emplace_back("mtime", std::to_string(e.mtimeSeconds));
          following.push_back(std::move(entry));
        }
        return {};
      });
}

// Tasks //////////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::renderShot(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  std::string error;
  const auto shotId = shotIdArgument(command.args[0], error);
  if (!shotId)
    return error;
  RenderShot request;
  request.shotId = *shotId;
  return sendRequest(std::move(request), deadline, modifiers, taskStarted());
}

CommandRunner::Failure CommandRunner::cancelTask(
    const Command &command, Deadline deadline, Modifiers modifiers)
{
  unsigned long long taskId = 0;
  if (!parseNonNegative(command.args[0], taskId))
    return usageError(command);
  CancelTask request;
  request.taskId = taskId;
  return sendRequest(std::move(request), deadline, modifiers);
}

CommandRunner::Describe CommandRunner::taskStarted(TaskMessage message)
{
  return [message](CommandRunner &self,
             const ProjectOpReply &reply,
             Event &event,
             std::vector<Event> &) -> Failure {
    const auto started = results<TaskStartedResult>(reply);
    if (!started)
      return "the reply carries no TaskStartedResult";
    event.fields.emplace_back("taskId", std::to_string(started->taskId));
    self.m_variables["lastTaskId"] = std::to_string(started->taskId);
    self.m_taskMessages[started->taskId] = message;
    return {};
  };
}

// Shot arguments /////////////////////////////////////////////////////////////

const Shot *CommandRunner::replicaShot(
    const std::string &id, std::string &error) const
{
  const auto resolved = shotIdArgument(id, error);
  if (!resolved)
    return nullptr;
  const auto *shot = project::findShot(*m_session->project(), *resolved);
  if (!shot)
    error = "the replica has no shot '" + *resolved + "'";
  return shot;
}

std::optional<std::string> CommandRunner::shotIdArgument(
    const std::string &text, std::string &error) const
{
  const auto *project = m_session->project();
  if (!project) {
    error = "no Project Replica";
    return {};
  }
  if (text == "active")
    return project->activeShotId;
  return text;
}

} // namespace vsr::scivis_studio::test_client
