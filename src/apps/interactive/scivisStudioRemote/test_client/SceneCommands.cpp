// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/*
 * The CommandRunner handlers that work on the client-held state and the
 * one-way messages: scene edits (set-param, remove-param,
 * set-node-transform), playback and the viewport (set-time, pick,
 * set-outline, viewport-settings, find-object), inspection (dump-scene,
 * dump-layers, dump-project, dump-frame), the UI state round trip
 * (set-ui-state, dump-ui-state) and assert. The table in CommandRunner.cpp
 * has checked each command's argument count before a handler runs.
 */

#include "AnyText.h"
#include "CommandRunner.h"
#include "CommandText.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
// vsr_scivis_studio_model
#include "Project.h"
#include "Shot.h"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Object.hpp"
// vsr_core
#include "vsr/core/ObjectPool.hpp"
// std
#include <cstring>
#include <limits>
#include <sstream>

namespace vsr::scivis_studio::test_client {

using namespace protocol;
using vsr::core::Any;

namespace {

// Numbers compare numerically at float32 precision for equality (the scene's
// values are float32, so `== 0.9` must hold for a value set from "0.9");
// anything else compares as text.
bool compareValues(const std::string &lhs,
    const std::string &op,
    const std::string &rhs,
    bool &result,
    std::string &error)
{
  if (op == "contains") {
    result = lhs.find(rhs) != std::string::npos;
    return true;
  }

  double a = 0;
  double b = 0;
  int cmp = 0;
  if (parseDouble(lhs, a) && parseDouble(rhs, b)) {
    if (float(a) == float(b))
      cmp = 0;
    else
      cmp = a < b ? -1 : 1;
  } else {
    cmp = lhs.compare(rhs);
    cmp = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
  }

  if (op == "==")
    result = cmp == 0;
  else if (op == "!=")
    result = cmp != 0;
  else if (op == "<")
    result = cmp < 0;
  else if (op == "<=")
    result = cmp <= 0;
  else if (op == ">")
    result = cmp > 0;
  else if (op == ">=")
    result = cmp >= 0;
  else {
    error = "unknown operator '" + op + "'; valid: == != < <= > >= contains";
    return false;
  }
  return true;
}

constexpr const char *VIEWPORT_SETTINGS_KEYS =
    "highlightSelection, outlinePrimitives, showWorldBounds,"
    " worldBoundsColor=r,g,b,a, worldBoundsWidth, visualizeAOV (NONE, DEPTH,"
    " ALBEDO, NORMAL, EDGES, OBJECT_ID, PRIMITIVE_ID, INSTANCE_ID),"
    " depthVisualMinimum, depthVisualMaximum, edgeInvert";

// Applies one `key=value` of viewport-settings; false with the reason on an
// unknown key or a value the field cannot hold.
bool applyViewportField(ViewportSettings &settings,
    const std::string &key,
    const std::string &value,
    std::string &error)
{
  const auto badValue = [&] {
    error = "not a valid " + key + ": " + value;
    return false;
  };
  const auto flag = [&](bool &field) {
    return parseBool(value, field) ? true : badValue();
  };
  const auto number = [&](float &field) {
    double v = 0;
    if (!parseDouble(value, v))
      return badValue();
    field = float(v);
    return true;
  };

  if (key == "highlightSelection")
    return flag(settings.highlightSelection);
  if (key == "outlinePrimitives")
    return flag(settings.outlinePrimitives);
  if (key == "showWorldBounds")
    return flag(settings.showWorldBounds);
  if (key == "edgeInvert")
    return flag(settings.edgeInvert);
  if (key == "depthVisualMinimum")
    return number(settings.depthVisualMinimum);
  if (key == "depthVisualMaximum")
    return number(settings.depthVisualMaximum);
  if (key == "worldBoundsWidth") {
    long long width = 0;
    if (!parseInteger(value, width) || width < 0
        || width > std::numeric_limits<int>::max())
      return badValue();
    settings.worldBoundsWidth = int(width);
    return true;
  }
  if (key == "worldBoundsColor") {
    std::stringstream list(value);
    std::string item;
    float rgba[4];
    size_t n = 0;
    while (std::getline(list, item, ',')) {
      double v = 0;
      if (n == 4 || !parseDouble(item, v))
        return badValue();
      rgba[n++] = float(v);
    }
    if (n != 4)
      return badValue();
    settings.worldBoundsColor = {rgba[0], rgba[1], rgba[2], rgba[3]};
    return true;
  }
  if (key == "visualizeAOV") {
    const auto aov = aovTypeFromString(upper(value));
    if (!aov)
      return badValue();
    settings.visualizeAOV = *aov;
    return true;
  }
  error = "unknown viewport setting '" + key
      + "'; valid: " + VIEWPORT_SETTINGS_KEYS;
  return false;
}

} // namespace

// Scene edits ////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::setParam(const Command &command)
{
  SceneObjectRef ref;
  std::string error;
  if (!parseObjectRef(command.args[0], command.args[1], ref, error))
    return error;
  const auto valueType = parseAnariType(command.args[3]);
  if (!valueType)
    return "unknown ANARI type: " + command.args[3];
  Any value;
  const std::vector<std::string> tokens(
      command.args.begin() + 4, command.args.end());
  if (!anyFromTokens(*valueType, tokens, value, error))
    return error;
  if (!m_session->setParameter(ref, command.args[2], value, &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::removeParam(const Command &command)
{
  SceneObjectRef ref;
  std::string error;
  if (!parseObjectRef(command.args[0], command.args[1], ref, error))
    return error;
  if (!m_session->removeParameter(ref, command.args[2], &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::setNodeTransform(const Command &command)
{
  SceneNodeRef node;
  node.layerName = command.args[0];
  unsigned long long index = 0;
  if (!parseNonNegative(command.args[1], index))
    return "not a node index: " + command.args[1];
  node.nodeIndex = size_t(index);
  float values[16];
  for (size_t i = 0; i < 16; ++i) {
    double v = 0;
    if (!parseDouble(command.args[2 + i], v))
      return "not a number: " + command.args[2 + i];
    values[i] = float(v);
  }
  vsr::math::mat4 transform;
  std::memcpy(&transform, values, sizeof(values));
  std::string error;
  if (!m_session->setNodeTransform(node, transform, &error))
    return error;
  return drainEvents();
}

// Playback and the viewport ///////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::setTime(const Command &command)
{
  long long frame = 0;
  if (!parseInteger(command.args[1], frame)
      || frame < std::numeric_limits<int>::min()
      || frame > std::numeric_limits<int>::max())
    return usageError(command);
  std::string error;
  const auto shotId = shotIdArgument(command.args[0], error);
  if (!shotId)
    return error;
  // Optimistic and latest-wins: nothing to await. The rest snapshot the
  // server debounces is await-snapshot's.
  SetTime scrub;
  scrub.shotId = *shotId;
  scrub.frame = int(frame);
  if (!m_session->send(scrub, &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::pick(
    const Command &command, Deadline deadline)
{
  long long x = 0;
  long long y = 0;
  if (!parseInteger(command.args[0], x) || !parseInteger(command.args[1], y))
    return usageError(command);
  // Frame-header pixels, x right and y down from the top-left; the server
  // clamps what lies outside the frame.
  Pick request;
  request.requestId = m_session->nextRequestId();
  request.x = int(x);
  request.y = int(y);
  m_variables["lastRequestId"] = std::to_string(request.requestId);
  std::string error;
  if (!m_session->send(request, &error))
    return error;

  // A PickReply is a plain message, not a ProjectOpReply, but it is matched
  // the same way: by the request id in its record.
  const auto idText = std::to_string(request.requestId);
  const PickReply *reply = nullptr;
  const auto wait = pumpUntilEvent(
      [&](const Event &event) {
        if (event.name != "PickReply")
          return false;
        for (const auto &[key, value] : event.fields) {
          if (key == "requestId" && value == idText) {
            reply = m_session->pickReply(request.requestId);
            return true;
          }
        }
        return false;
      },
      deadline);
  if (wait != Wait::Done)
    return waitFailure(wait, "the PickReply to request " + idText, deadline);
  if (!reply)
    return "the PickReply to request " + idText + " did not decode";
  m_lastPick = *reply;
  // The identity feeds set-outline; a miss leaves the variables unset so a
  // script that outlines what it did not hit FAILs by name.
  if (reply->objectIdentity) {
    m_variables["lastPickType"] = shortTypeName(reply->objectIdentity->type);
    m_variables["lastPickIndex"] =
        std::to_string(reply->objectIdentity->objectIndex);
  } else {
    m_variables.erase("lastPickType");
    m_variables.erase("lastPickIndex");
  }
  return {};
}

CommandRunner::Failure CommandRunner::setOutline(const Command &command)
{
  SetOutline outline;
  if (!(command.args.empty()
          || (command.args.size() == 1 && lower(command.args[0]) == "none"))) {
    SceneObjectRef ref;
    size_t consumed = 0;
    std::string error;
    if (!parseObjectRefArgs(command.args, 0, ref, consumed, error)
        || consumed != command.args.size())
      return usageError(command);
    outline.objectIdentity = ref;
  }
  std::string error;
  if (!m_session->send(outline, &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::viewportSettings(const Command &command)
{
  // Edits land on the remembered copy, which goes out whole every time, so
  // `viewport-settings visualizeAOV=DEPTH` then `... showWorldBounds=on`
  // leaves both in force. With no edits the current copy is sent again.
  auto settings = m_viewportSettings;
  std::string error;
  for (const auto &edit : command.args) {
    const auto eq = edit.find('=');
    if (eq == std::string::npos || eq == 0) {
      return usageError(command) + " with keys "
          + std::string(VIEWPORT_SETTINGS_KEYS);
    }
    if (!applyViewportField(
            settings, edit.substr(0, eq), edit.substr(eq + 1), error))
      return error;
  }
  if (!m_session->send(settings, &error))
    return error;
  m_viewportSettings = settings;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::findObject(const Command &command)
{
  auto type = parseAnariType(command.args[0]);
  if (!type || !anari::isObject(*type))
    return "not a scene object type: " + command.args[0];
  // Every array kind lives in the mirror's one array pool.
  if (anari::isArray(*type))
    type = ANARI_ARRAY;
  std::optional<std::string> wanted;
  if (command.args.size() == 2) {
    const auto &selector = command.args[1];
    if (selector.rfind("name=", 0) == 0)
      wanted = selector.substr(5);
    else if (selector != "first")
      return usageError(command);
  }
  if (const auto pending = drainEvents())
    return pending;

  const vsr::scene::Object *found = nullptr;
  forEachObjectPool(m_session->mirror().objectDB(),
      [&](anari::DataType poolType, const auto &pool) {
        if (poolType != *type)
          return;
        vsr::core::foreach_item_const(pool, [&](const vsr::scene::Object *obj) {
          if (!obj || found)
            return;
          if (!wanted || obj->name() == *wanted)
            found = obj;
        });
      });
  if (!found) {
    std::string what = "no " + shortTypeName(*type);
    if (wanted)
      what += " named \"" + *wanted + "\"";
    return what + " in the mirror";
  }
  SceneObjectRef ref;
  ref.type = *type;
  ref.objectIndex = found->index();
  m_variables["lastObjectRef"] = objectRefText(ref);
  m_variables["lastObjectType"] = shortTypeName(*type);
  m_variables["lastObjectIndex"] = std::to_string(ref.objectIndex);
  printRecord("EVT Object type=" + shortTypeName(*type)
      + " index=" + std::to_string(found->index())
      + " subtype=" + found->subtype().str() + " name=" + quoted(found->name())
      + " params=" + std::to_string(found->numParameters()));
  return {};
}

// Inspection /////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::dumpScene(const Command &)
{
  if (const auto pending = drainEvents())
    return pending;
  forEachObjectPool(m_session->mirror().objectDB(),
      [&](anari::DataType type, const auto &pool) {
        vsr::core::foreach_item_const(pool, [&](const vsr::scene::Object *obj) {
          if (!obj)
            return;
          printRecord("EVT Object type=" + shortTypeName(type)
              + " index=" + std::to_string(obj->index()) + " subtype="
              + obj->subtype().str() + " name=" + quoted(obj->name())
              + " params=" + std::to_string(obj->numParameters()));
        });
      });
  return {};
}

CommandRunner::Failure CommandRunner::dumpLayers(const Command &)
{
  if (const auto pending = drainEvents())
    return pending;
  const auto &scene = m_session->mirror();
  for (size_t i = 0; i < scene.numberOfLayers(); ++i) {
    const auto *layer = scene.layer(i);
    if (!layer)
      continue;
    printRecord("EVT Layer index=" + std::to_string(i) + " name="
        + quoted(layer->name()) + " nodes=" + std::to_string(layer->size())
        + " active=" + boolText(scene.layerIsActive(layer->name())));
  }
  return {};
}

CommandRunner::Failure CommandRunner::dumpProject(const Command &)
{
  if (const auto pending = drainEvents())
    return pending;
  const auto *project = m_session->project();
  if (!project)
    return "no Project Replica";
  printRecord("EVT Project name=" + quoted(project->name)
      + " activeShot=" + project->activeShotId
      + " shots=" + std::to_string(project->shots.size())
      + " datasets=" + std::to_string(project->datasets.size())
      + " lightRigs=" + std::to_string(project->lightRigs.size())
      + " cameraRigs=" + std::to_string(project->cameraRigs.size())
      + " colorMaps=" + std::to_string(project->colorMaps.size())
      + " dirty=" + boolText(project->dirty)
      + " directory=" + quoted(project->projectDirectory.generic_string()));
  for (const auto &shot : project->shots) {
    printRecord("EVT Shot id=" + shot.id + " name=" + quoted(shot.name)
        + " frameCount=" + std::to_string(shot.frameCount)
        + " fps=" + numberText(shot.fps) + " currentFrame="
        + std::to_string(shot.currentFrame) + " loop=" + boolText(shot.loop)
        + " lightRigId=" + shot.lightRigId + " cameraRigId=" + shot.cameraRigId
        + " bindings=" + std::to_string(shot.datasetBindings.size())
        + " camera=" + objectRefText(shot.camera)
        + " active=" + boolText(shot.id == project->activeShotId));
  }
  for (const auto &dataset : project->datasets) {
    printRecord("EVT Dataset id=" + dataset.id + " name=" + quoted(dataset.name)
        + " status=" + dataset::toString(dataset.status)
        + " residency=" + dataset::toString(dataset.residency)
        + " sourceKind=" + dataset::toString(dataset.sourceKind)
        + " importerType=" + dataset.importerType + " rootNode="
        + nodeText(dataset.rootNode) + " dirty=" + boolText(dataset.dirty));
  }
  for (const auto &rig : project->lightRigs) {
    printRecord("EVT LightRig id=" + rig.id + " name=" + quoted(rig.name)
        + " rootNode=" + nodeText(rig.rootNode));
  }
  for (const auto &rig : project->cameraRigs) {
    printRecord("EVT CameraRig id=" + rig.id + " name=" + quoted(rig.name)
        + " keyframes=" + std::to_string(rig.keyframes.size()));
  }
  for (const auto &map : project->colorMaps)
    printRecord("EVT ColorMap id=" + map.id + " name=" + quoted(map.name));
  return {};
}

CommandRunner::Failure CommandRunner::dumpFrame(const Command &)
{
  if (const auto pending = drainEvents())
    return pending;
  const auto &header = m_session->lastFrameHeader();
  if (!header)
    return "no frame received yet";
  const auto view = decodeFrame(m_session->lastFrame());
  printRecord("EVT " + frameEvent(*header, view ? view->size : 0).text());
  return {};
}

// UI state ///////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::setUIState(const Command &command)
{
  if (command.args.size() == 1 && lower(command.args[0]) == "none") {
    // Back to sending no tree: the server then keeps the one it retains.
    m_uiStateToSave.reset();
    return {};
  }
  // The shape the GUI saves is {windows/<Window>/..., layout, settings/...};
  // the server reads no further than those child names, so string leaves
  // under windows/ are all a round trip needs. Repeated commands compose:
  // a key set again is overwritten, the others stay.
  auto tree = m_uiStateToSave ? m_uiStateToSave : makeSubtree();
  auto &windows = tree->root()["windows"];
  for (const auto &edit : command.args) {
    const auto eq = edit.find('=');
    if (eq == std::string::npos || eq == 0)
      return "not a <key>=<value> edit: " + edit;
    windows[edit.substr(0, eq)] = edit.substr(eq + 1);
  }
  m_uiStateToSave = tree;
  return {};
}

CommandRunner::Failure CommandRunner::dumpUIState(const Command &)
{
  if (const auto pending = drainEvents())
    return pending;
  const auto &tree = m_session->uiState();
  printRecord(std::string("EVT UIState present=") + boolText(tree != nullptr)
      + " children=" + std::to_string(tree ? tree->root().numChildren() : 0));
  if (!tree)
    return {};
  // One line per leaf, its path from the root written with slashes.
  const std::function<void(const vsr::core::DataNode &, const std::string &)>
      walk = [&](const vsr::core::DataNode &node, const std::string &path) {
        if (node.numChildren() == 0) {
          printRecord("EVT UIStateEntry path=" + quoted(path)
              + " value=" + quoted(anyText(node.getValue())));
          return;
        }
        node.foreach_child_const([&](const vsr::core::DataNode &child) {
          walk(child, path.empty() ? child.name() : path + "/" + child.name());
        });
      };
  tree->root().foreach_child_const(
      [&](const vsr::core::DataNode &child) { walk(child, child.name()); });
  return {};
}

// Assertions /////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::assertValue(const Command &command)
{
  if (const auto pending = drainEvents())
    return pending;
  std::string error;
  const auto lhs = namedValue(command.args[0], error);
  if (!lhs)
    return error;
  // `@name` on the right compares two named values (the rest frame against
  // the last header's); anything else is the literal it reads as.
  std::optional<std::string> resolved;
  if (command.args[2].size() > 1 && command.args[2][0] == '@') {
    resolved = namedValue(command.args[2].substr(1), error);
    if (!resolved)
      return error;
  }
  const std::string rhs = resolved ? *resolved : command.args[2];
  bool holds = false;
  if (!compareValues(*lhs, command.args[1], rhs, holds, error))
    return error;
  if (!holds) {
    std::string expected = quoted(command.args[2]);
    if (rhs != command.args[2])
      expected += " (" + quoted(rhs) + ")";
    return command.args[0] + " is " + quoted(*lhs) + ", not " + command.args[1]
        + " " + expected;
  }
  return {};
}

} // namespace vsr::scivis_studio::test_client
