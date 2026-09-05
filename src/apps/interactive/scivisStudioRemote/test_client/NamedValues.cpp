// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/*
 * What `assert` can name: assertNames() lists the values as --help shows
 * them and namedValue() reads one off the session, the mirror, the replica
 * or the runner's own state.
 */

#include "AnyText.h"
#include "CommandRunner.h"
#include "CommandText.h"
// vsr_scivis_studio_model
#include "CameraRig.h"
#include "LightRig.h"
#include "Project.h"
#include "Shot.h"
// vsr_scene
#include "vsr/scene/Object.hpp"
#include "vsr/scene/Parameter.hpp"

namespace vsr::scivis_studio::test_client {

using namespace protocol;

const std::vector<std::string> &CommandRunner::assertNames()
{
  static const std::vector<std::string> names = {"state",
      "scene.objects",
      "scene.layers",
      "scene.cameras",
      "scene.renderers",
      "project.name",
      "project.directory",
      "project.activeShot",
      "project.dirty",
      "project.shots",
      "project.datasets",
      "project.lightRigs",
      "project.cameraRigs",
      "project.colorMaps",
      "shot.<id>.<field>",
      "shot.active.<field>",
      "dataset.<id>.<field>",
      "lightRig.<id>.name",
      "cameraRig.<id>.name",
      "colorMap.<id>.name",
      "tasks.completed",
      "tasks.failed",
      "tasks.replayed",
      "task.<id>.<field>",
      "task.last.<field>",
      "uiState.present",
      "uiState.<key>",
      "replies.failed",
      "replies.pending",
      "snapshots.received",
      "browse.entries",
      "var.<name>",
      "frame.width",
      "frame.height",
      "frame.encoding",
      "frame.shotId",
      "frame.frame",
      "frames.received",
      "frames.advanced",
      "frames.maxStep",
      "frameConfig.width",
      "frameConfig.height",
      "param.<type>.<index>.<name>",
      "errors.received",
      "lastError",
      "lastReplyError",
      "warnings.received",
      "lastWarning",
      "pick.hit",
      "pick.worldPosition",
      "pick.objectType",
      "pick.objectIndex",
      "histogram.bins",
      "histogram.min",
      "histogram.max",
      "histogram.total",
      "histogram.nonFinite"};
  return names;
}

std::optional<std::string> CommandRunner::namedValue(
    const std::string &name, std::string &error)
{
  const auto &scene = m_session->mirror();
  const auto *project = m_session->project();
  const auto &frame = m_session->lastFrameHeader();

  const auto needProject = [&]() -> std::optional<std::string> {
    error = name + ": no Project Replica";
    return {};
  };
  const auto needFrame = [&]() -> std::optional<std::string> {
    error = name + ": no frame received yet";
    return {};
  };

  if (name == "state")
    return std::string(toString(m_session->state()));
  if (name == "scene.objects")
    return std::to_string(totalObjects(scene));
  if (name == "scene.layers")
    return std::to_string(scene.numberOfLayers());
  if (name == "scene.cameras")
    return std::to_string(scene.numberOfObjects(ANARI_CAMERA));
  if (name == "scene.renderers")
    return std::to_string(scene.numberOfObjects(ANARI_RENDERER));
  if (name.rfind("project.", 0) == 0) {
    if (!project)
      return needProject();
    const auto field = name.substr(8);
    if (field == "activeShot")
      return project->activeShotId;
    if (field == "name")
      return project->name;
    if (field == "directory")
      return project->projectDirectory.generic_string();
    if (field == "dirty")
      return std::string(boolText(project->dirty));
    if (field == "shots")
      return std::to_string(project->shots.size());
    if (field == "datasets")
      return std::to_string(project->datasets.size());
    if (field == "lightRigs")
      return std::to_string(project->lightRigs.size());
    if (field == "cameraRigs")
      return std::to_string(project->cameraRigs.size());
    if (field == "colorMaps")
      return std::to_string(project->colorMaps.size());
  }
  if (name == "tasks.completed")
    return std::to_string(m_session->tasksCompleted());
  if (name == "tasks.failed")
    return std::to_string(m_session->tasksFailed());
  if (name == "tasks.replayed")
    return std::to_string(m_session->tasksReplayed());
  if (name.rfind("task.", 0) == 0) {
    // task.<id>.<field>, `last` for $lastTaskId.
    const auto idEnd = name.find('.', 5);
    if (idEnd == std::string::npos || idEnd + 1 >= name.size()) {
      error = "malformed value '" + name + "'; use task.<id|last>.<field>";
      return {};
    }
    const auto idText = name.substr(5, idEnd - 5);
    const auto field = name.substr(idEnd + 1);
    unsigned long long taskId = 0;
    if (idText == "last") {
      const auto last = variable("lastTaskId");
      if (!last) {
        error = name + ": no task has been started yet ($lastTaskId is unset)";
        return {};
      }
      parseNonNegative(*last, taskId);
    } else if (!parseNonNegative(idText, taskId)) {
      error = name + ": not a task id: " + idText;
      return {};
    }
    const auto *task = m_session->task(taskId);
    if (!task) {
      error =
          name + ": nothing has been heard of task " + std::to_string(taskId);
      return {};
    }
    if (field == "state")
      return std::string(toString(task->status));
    if (field == "message")
      return task->message;
    if (field == "framesCompleted")
      return std::to_string(task->framesCompleted);
    if (field == "current")
      return std::to_string(task->current);
    if (field == "total")
      return std::to_string(task->total);
    error = name + ": unknown task field '" + field
        + "'; valid: state, message, framesCompleted, current, total";
    return {};
  }
  if (name.rfind("uiState.", 0) == 0) {
    const auto &tree = m_session->uiState();
    const auto key = name.substr(8);
    if (key == "present")
      return std::string(boolText(tree != nullptr));
    if (!tree) {
      error = name + ": the server has sent no UIState tree";
      return {};
    }
    // What set-ui-state writes: a leaf under windows/.
    const auto *windows = tree->root().child("windows");
    const auto *leaf = windows ? windows->child(key) : nullptr;
    if (!leaf) {
      error = name + ": the UIState tree has no windows/" + key;
      return {};
    }
    return anyText(leaf->getValue());
  }
  if (name == "replies.failed")
    return std::to_string(m_session->repliesFailed());
  if (name == "replies.pending")
    return std::to_string(m_pendingReplies.size());
  if (name == "snapshots.received")
    return std::to_string(m_session->snapshotsReceived());
  if (name == "browse.entries")
    return std::to_string(m_browseEntries.size());
  if (name.rfind("var.", 0) == 0) {
    // The variable itself, since `$name` in this position would expand to a
    // value name.
    const auto value = variable(name.substr(4));
    if (!value)
      error = name + ": unknown variable $" + name.substr(4);
    return value;
  }

  // <collection>.<id>.<field>: ids carry no dots, fields may.
  for (const char *collection :
      {"shot", "dataset", "lightRig", "cameraRig", "colorMap"}) {
    const std::string prefix = std::string(collection) + ".";
    if (name.rfind(prefix, 0) != 0)
      continue;
    const auto idEnd = name.find('.', prefix.size());
    if (idEnd == std::string::npos || idEnd + 1 >= name.size()) {
      error =
          "malformed value '" + name + "'; use " + collection + ".<id>.<field>";
      return {};
    }
    if (!project)
      return needProject();
    auto id = name.substr(prefix.size(), idEnd - prefix.size());
    const auto field = name.substr(idEnd + 1);
    if (prefix == "shot." && id == "active")
      id = project->activeShotId;
    const auto missing = [&](const char *what) -> std::optional<std::string> {
      error = name + ": the replica has no " + what + " '" + id + "'";
      return {};
    };
    const auto noField = [&]() -> std::optional<std::string> {
      error = name + ": unknown " + collection + " field '" + field + "'";
      return {};
    };
    if (prefix == "shot.") {
      const auto *shot = project::findShot(*project, id);
      if (!shot)
        return missing("shot");
      if (field == "name")
        return shot->name;
      if (field == "frameCount")
        return std::to_string(shot->frameCount);
      if (field == "fps")
        return numberText(shot->fps);
      if (field == "currentFrame")
        return std::to_string(shot->currentFrame);
      if (field == "loop")
        return std::string(boolText(shot->loop));
      if (field == "playing")
        return std::string(boolText(shot->playing));
      if (field == "lightRigId")
        return shot->lightRigId;
      if (field == "cameraRigId")
        return shot->cameraRigId;
      if (field == "camera")
        return objectRefText(shot->camera);
      if (field == "bindings")
        return std::to_string(shot->datasetBindings.size());
      if (field == "renderSettings.width")
        return std::to_string(shot->renderSettings.width);
      if (field == "renderSettings.height")
        return std::to_string(shot->renderSettings.height);
      if (field == "renderSettings.samples")
        return std::to_string(shot->renderSettings.samples);
      if (field == "renderSettings.rendererLibrary")
        return shot->renderSettings.rendererLibrary;
      if (field == "renderSettings.rendererSubtype")
        return shot->renderSettings.rendererSubtype;
      if (field == "renderSettings.outputFilePrefix")
        return shot->renderSettings.outputFilePrefix;
      if (field.rfind("binding.", 0) == 0 && field.size() > 8) {
        const auto *binding = shot::findDatasetBinding(*shot, field.substr(8));
        if (!binding) {
          error = name + ": shot '" + id + "' has no binding for '"
              + field.substr(8) + "'";
          return {};
        }
        return std::string(boolText(binding->enabled));
      }
      return noField();
    }
    if (prefix == "dataset.") {
      const auto *dataset = project::findDataset(*project, id);
      if (!dataset)
        return missing("dataset");
      if (field == "name")
        return dataset->name;
      if (field == "status")
        return std::string(dataset::toString(dataset->status));
      if (field == "residency")
        return std::string(dataset::toString(dataset->residency));
      if (field == "sourceKind")
        return std::string(dataset::toString(dataset->sourceKind));
      if (field == "importerType")
        return dataset->importerType;
      if (field == "sourcePath")
        return dataset->source.sourcePath;
      if (field == "dirty")
        return std::string(boolText(dataset->dirty));
      if (field == "declared")
        return std::string(boolText(dataset->declared));
      if (field == "rootNode")
        return nodeText(dataset->rootNode);
      return noField();
    }
    if (field != "name")
      return noField();
    if (prefix == "lightRig.") {
      const auto *rig = light_rig::findLightRig(*project, id);
      return rig ? std::optional(rig->name) : missing("light rig");
    }
    if (prefix == "cameraRig.") {
      const auto *rig = camera_rig::findCameraRig(*project, id);
      return rig ? std::optional(rig->name) : missing("camera rig");
    }
    const auto *map = project::findColorMap(*project, id);
    return map ? std::optional(map->name) : missing("color map");
  }
  if (name == "frame.width")
    return frame ? std::optional(std::to_string(frame->width)) : needFrame();
  if (name == "frame.height")
    return frame ? std::optional(std::to_string(frame->height)) : needFrame();
  if (name == "frame.encoding") {
    return frame ? std::optional(std::string(toString(frame->encoding)))
                 : needFrame();
  }
  if (name == "frame.shotId")
    return frame ? std::optional(frame->shotId) : needFrame();
  if (name == "frame.frame")
    return frame ? std::optional(std::to_string(frame->frame)) : needFrame();
  if (name == "frames.received")
    return std::to_string(m_session->framesReceived());
  if (name == "frames.advanced")
    return std::to_string(m_session->framesAdvanced());
  if (name == "frames.maxStep")
    return std::to_string(m_session->frameMaxStep());
  if (name == "frameConfig.width")
    return std::to_string(m_session->frameConfig().width);
  if (name == "frameConfig.height")
    return std::to_string(m_session->frameConfig().height);
  if (name == "errors.received")
    return std::to_string(m_session->errorsReceived());
  if (name == "lastError")
    return m_session->lastError();
  if (name == "lastReplyError")
    return m_session->lastReplyError();
  if (name == "warnings.received")
    return std::to_string(m_session->warningsReceived());
  if (name == "lastWarning") {
    const auto &warning = m_session->lastWarning();
    return warning ? warning->message : std::string();
  }
  if (name.rfind("pick.", 0) == 0) {
    if (!m_lastPick) {
      error = name + ": no pick has been answered yet";
      return {};
    }
    const auto field = name.substr(5);
    const auto &identity = m_lastPick->objectIdentity;
    if (field == "hit")
      return std::string(boolText(m_lastPick->hit));
    if (field == "worldPosition") {
      const auto &p = m_lastPick->worldPosition;
      return numberText(p.x) + " " + numberText(p.y) + " " + numberText(p.z);
    }
    if (field == "objectType")
      return identity ? shortTypeName(identity->type) : std::string("none");
    if (field == "objectIndex") {
      return identity ? std::to_string(identity->objectIndex)
                      : std::string("none");
    }
  }
  if (name.rfind("histogram.", 0) == 0) {
    if (!m_histogram) {
      error = name + ": no histogram has been answered yet";
      return {};
    }
    const auto field = name.substr(10);
    if (field == "bins")
      return std::to_string(m_histogram->bins.size());
    if (field == "min")
      return numberText(m_histogram->minValue);
    if (field == "max")
      return numberText(m_histogram->maxValue);
    if (field == "total") {
      uint64_t total = 0;
      for (const auto count : m_histogram->bins)
        total += count;
      return std::to_string(total);
    }
    if (field == "nonFinite")
      return std::to_string(m_histogram->nonFinite);
  }

  if (name.rfind("param.", 0) == 0) {
    // param.<type>.<index>.<name>; the parameter name may itself hold dots.
    const auto typeEnd = name.find('.', 6);
    const auto indexEnd =
        typeEnd == std::string::npos ? typeEnd : name.find('.', typeEnd + 1);
    if (typeEnd == std::string::npos || indexEnd == std::string::npos
        || indexEnd + 1 >= name.size()) {
      error = "malformed value '" + name + "'; use param.<type>.<index>.<name>";
      return {};
    }
    SceneObjectRef ref;
    if (!parseObjectRef(name.substr(6, typeEnd - 6),
            name.substr(typeEnd + 1, indexEnd - typeEnd - 1),
            ref,
            error))
      return {};
    const auto *obj = scene.getObject(ref.type, ref.objectIndex);
    if (!obj) {
      error = name + ": no " + shortTypeName(ref.type) + " "
          + std::to_string(ref.objectIndex) + " in the mirror";
      return {};
    }
    const auto paramName = name.substr(indexEnd + 1);
    const auto *param = obj->parameter(paramName.c_str());
    if (!param) {
      error = name + ": " + shortTypeName(ref.type) + " "
          + std::to_string(ref.objectIndex) + " has no parameter '" + paramName
          + "'";
      return {};
    }
    return anyText(param->value());
  }

  error = "unknown value '" + name + "'; valid: " + join(assertNames(), ", ");
  return {};
}

} // namespace vsr::scivis_studio::test_client
