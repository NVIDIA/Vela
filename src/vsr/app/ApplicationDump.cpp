// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ApplicationDump.h"

#include "Context.h"
#include "LegacyApplicationContext.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/archives/AnimationManagerArchive.hpp"
#include "vsr/io/archives/SceneArchive.hpp"

namespace vsr::app {

namespace {

void deserializeStableContextSettings(Context &context, core::DataNode &root)
{
  if (auto *deviceManager = root.child("ANARIDeviceManager"))
    context.anari.loadSettings(*deviceManager);
  if (auto *offlineRendering = root.child("offlineRendering"))
    context.offline.loadSettings(*offlineRendering);
  if (auto *settings = root.child("settings")) {
    bool value = context.logVerbose();
    if (auto *logVerbose = settings->child("logVerbose");
        logVerbose && logVerbose->getValue(ANARI_BOOL, &value)) {
      context.setLogVerbose(value);
    }
    value = context.logEchoOutput();
    if (auto *logEchoOutput = settings->child("logEchoOutput");
        logEchoOutput && logEchoOutput->getValue(ANARI_BOOL, &value)) {
      context.setLogEchoOutput(value);
    }
  }

  context.view.poses.clear();
  if (auto *cameraPoses = root.child("cameraPoses")) {
    cameraPoses->foreach_child([&](core::DataNode &node) {
      rendering::CameraPose pose;
      deserialize_CameraPose(node, pose);
      context.view.poses.push_back(std::move(pose));
    });
  }
}

bool deserializeApplicationArchives(VSRState &state,
    core::DataNode &sceneArchive,
    core::DataNode &animationManagerArchive)
{
  state.animationMgr.stop();
  state.animationMgr.removeAllAnimations();
  return io::deserialize_SceneArchive(state.scene, sceneArchive)
      && io::deserialize_AnimationManagerArchive(
          state.animationMgr, animationManagerArchive);
}

} // namespace

void serialize_CameraPose(
    const rendering::CameraPose &pose, core::DataNode &node)
{
  node["name"] = pose.name;
  node["lookat"] = pose.lookat;
  node["azeldist"] = pose.azeldist;
  node["fixedDist"] = pose.fixedDist;
  node["upAxis"] = pose.upAxis;
  node["mode"] = pose.mode;
}

bool deserialize_CameraPose(
    const core::DataNode &node, rendering::CameraPose &pose)
{
  auto read = [&](const char *name, anari::DataType type, void *out) {
    const auto *field = node.child(name);
    return !field || field->getValue(type, out);
  };
  return read("name", ANARI_STRING, &pose.name)
      && read("lookat", ANARI_FLOAT32_VEC3, &pose.lookat)
      && read("azeldist", ANARI_FLOAT32_VEC3, &pose.azeldist)
      && read("fixedDist", ANARI_FLOAT32, &pose.fixedDist)
      && read("upAxis", ANARI_INT32, &pose.upAxis)
      && read("mode", ANARI_INT32, &pose.mode);
}

bool serialize_ApplicationDump(const Context &context, core::DataNode &root)
{
  auto &archives = root["archives"];
  if (!io::serialize_SceneAndAnimationManagerArchives(context.vsr.scene,
          context.vsr.animationMgr,
          archives["scene"],
          archives["animationManager"])) {
    return false;
  }

  context.anari.saveSettings(root["ANARIDeviceManager"]);
  context.offline.saveSettings(root["offlineRendering"]);

  auto &settings = root["settings"];
  settings["logVerbose"] = context.logVerbose();
  settings["logEchoOutput"] = context.logEchoOutput();

  auto &cameraPoses = root["cameraPoses"];
  cameraPoses.reset();
  for (const auto &pose : context.view.poses)
    serialize_CameraPose(pose, cameraPoses.append());

  return true;
}

bool deserialize_ApplicationDump(Context &context, core::DataNode &root)
{
  auto *archives = root.child("archives");
  if (!archives) {
    auto *legacyContext = root.child("context");
    auto &legacyPayload = legacyContext ? *legacyContext : root;
    if (!io::validate_SceneArchive(legacyPayload).accepted())
      return false;

    auto &animationManager = context.vsr.animationMgr;
    animationManager.stop();
    animationManager.removeAllAnimations();
    if (!detail::deserializeLegacyApplicationContext(context, legacyPayload)) {
      return false;
    }
    deserializeStableContextSettings(context, root);
    return true;
  }

  auto *sceneArchive = archives->child("scene");
  auto *animationManagerArchive = archives->child("animationManager");
  if (!sceneArchive || !animationManagerArchive) {
    core::logError(
        "[deserialize_ApplicationDump] Application Dump requires "
        "archives/scene and archives/animationManager");
    return false;
  }
  VSRState stagedState;
  if (!deserializeApplicationArchives(
          stagedState, *sceneArchive, *animationManagerArchive)) {
    return false;
  }

  if (!deserializeApplicationArchives(
          context.vsr, *sceneArchive, *animationManagerArchive)) {
    core::logError(
        "[deserialize_ApplicationDump] staged Archives failed during commit");
    return false;
  }

  deserializeStableContextSettings(context, root);
  return true;
}

} // namespace vsr::app
