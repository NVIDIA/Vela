// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CameraRig.h"

#include "DataNodeFields.h"
#include "Project.h"
#include "ProjectSerialization.h"

#include "vsr/app/ApplicationDump.h"
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/rendering/view/CameraPath.h"

#include <algorithm>
#include <cmath>

namespace vsr::scivis_studio::camera_rig {

// Collection lookups ////////////////////////////////////////////////////////

CameraRigID nextCameraRigId(const Project &project)
{
  return project::nextUnusedId("cameraRig", project.cameraRigs);
}

CameraRig *findCameraRig(Project &project, const CameraRigID &id)
{
  auto itr = std::find_if(project.cameraRigs.begin(),
      project.cameraRigs.end(),
      [&](const CameraRig &r) { return r.id == id; });
  return itr == project.cameraRigs.end() ? nullptr : &*itr;
}

const CameraRig *findCameraRig(const Project &project, const CameraRigID &id)
{
  auto itr = std::find_if(project.cameraRigs.begin(),
      project.cameraRigs.end(),
      [&](const CameraRig &r) { return r.id == id; });
  return itr == project.cameraRigs.end() ? nullptr : &*itr;
}

// Interpolation enum <-> string //////////////////////////////////////////////

const char *toString(CameraInterpolation interpolation)
{
  switch (interpolation) {
  case CameraInterpolation::Hold:
    return "Hold";
  case CameraInterpolation::Linear:
    return "Linear";
  case CameraInterpolation::EaseOut:
    return "Ease Out";
  case CameraInterpolation::EaseIn:
    return "Ease In";
  case CameraInterpolation::EaseOutIn:
    return "Ease Out + In";
  }
  return "Linear";
}

std::optional<CameraInterpolation> interpolationFromString(const std::string &s)
{
  return enumFromName(
      s, CameraInterpolation::Hold, CameraInterpolation::EaseOutIn, toString);
}

// Manipulator <-> stored pose ////////////////////////////////////////////////

ManipulatorState manipulatorStateFromManipulator(
    const vsr::rendering::Manipulator &m)
{
  ManipulatorState state;
  state.orbit.lookat = m.at();
  state.orbit.azeldist =
      vsr::math::float3(m.azel().x, m.azel().y, m.distance());
  state.orbit.fixedDist = m.fixedDistance();
  state.orbit.upAxis = static_cast<int>(m.axis());
  state.orbit.mode = static_cast<int>(m.mode());
  return state;
}

void applyManipulatorState(
    vsr::rendering::Manipulator &m, const ManipulatorState &state)
{
  m.setConfig(state.orbit);
  m.setFixedDistance(state.orbit.fixedDist);
}

// Keyframe animation /////////////////////////////////////////////////////////

void sortKeyframes(CameraRig &rig)
{
  std::stable_sort(rig.keyframes.begin(),
      rig.keyframes.end(),
      [](const CameraKeyframe &a, const CameraKeyframe &b) {
        return a.frame < b.frame;
      });
}

static float lerp(float t, float a, float b)
{
  return a + t * (b - a);
}

static vsr::math::float3 lerpVec3(
    float t, const vsr::math::float3 &a, const vsr::math::float3 &b)
{
  return vsr::math::float3{
      lerp(t, a.x, b.x), lerp(t, a.y, b.y), lerp(t, a.z, b.z)};
}

static float applyInterpolation(CameraInterpolation interpolation, float t)
{
  switch (interpolation) {
  case CameraInterpolation::Hold:
  case CameraInterpolation::Linear:
    return t;
  case CameraInterpolation::EaseOut:
    return t * t;
  case CameraInterpolation::EaseIn:
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
  case CameraInterpolation::EaseOutIn:
    return t * t * t * (t * (6.f * t - 15.f) + 10.f);
  }
  return t;
}

ManipulatorState sampleCameraRig(const CameraRig &rig, int frame)
{
  if (rig.keyframes.empty())
    return rig.current;

  if (rig.keyframes.size() == 1)
    return rig.keyframes.front().manipulator;

  auto keyframes = rig.keyframes;
  std::stable_sort(keyframes.begin(),
      keyframes.end(),
      [](const CameraKeyframe &a, const CameraKeyframe &b) {
        return a.frame < b.frame;
      });

  if (frame <= keyframes.front().frame)
    return keyframes.front().manipulator;

  for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
    const auto &a = keyframes[i];
    const auto &b = keyframes[i + 1];
    if (frame > b.frame)
      continue;

    if (a.interpolationToNext == CameraInterpolation::Hold
        || b.frame == a.frame)
      return a.manipulator;

    const float t = static_cast<float>(frame - a.frame)
        / static_cast<float>(b.frame - a.frame);
    const float interpolatedT = applyInterpolation(a.interpolationToNext, t);

    ManipulatorState out;
    out.orbit = a.manipulator.orbit;
    out.orbit.lookat = lerpVec3(
        interpolatedT, a.manipulator.orbit.lookat, b.manipulator.orbit.lookat);
    out.orbit.azeldist = vsr::rendering::lerpAzElDist(interpolatedT,
        a.manipulator.orbit.azeldist,
        b.manipulator.orbit.azeldist);
    out.orbit.fixedDist = lerp(interpolatedT,
        a.manipulator.orbit.fixedDist,
        b.manipulator.orbit.fixedDist);
    out.orbit.upAxis = a.manipulator.orbit.upAxis;
    out.orbit.mode = a.manipulator.orbit.mode;
    return out;
  }

  return keyframes.back().manipulator;
}

// Serialization //////////////////////////////////////////////////////////////

static void manipulatorStateToNode(
    const ManipulatorState &state, vsr::core::DataNode &node)
{
  vsr::app::serialize_CameraPose(state.orbit, node["orbit"]);
}

static bool nodeToManipulatorState(
    const vsr::core::DataNode &node, ManipulatorState &state)
{
  const auto *orbit = node.child("orbit");
  return !orbit || vsr::app::deserialize_CameraPose(*orbit, state.orbit);
}

static bool nodeToKeyframe(
    const vsr::core::DataNode &node, CameraKeyframe &keyframe)
{
  if (!readOptionalChild(node, "frame", keyframe.frame)
      || !readOptionalChild(node, "name", keyframe.name)
      || !readOptionalEnumChild(node,
          "interpolationToNext",
          keyframe.interpolationToNext,
          interpolationFromString))
    return false;
  const auto *manipulator = node.child("manipulator");
  return !manipulator
      || nodeToManipulatorState(*manipulator, keyframe.manipulator);
}

void cameraRigToNode(const CameraRig &rig, vsr::core::DataNode &node)
{
  manipulatorStateToNode(rig.current, node["current"]);
  auto &keyframes = node["keyframes"];
  for (const auto &keyframe : rig.keyframes) {
    auto &kf = keyframes.append();
    kf["frame"] = keyframe.frame;
    kf["name"] = keyframe.name;
    kf["interpolationToNext"] = toString(keyframe.interpolationToNext);
    manipulatorStateToNode(keyframe.manipulator, kf["manipulator"]);
  }
}

bool nodeToCameraRig(const vsr::core::DataNode &node, CameraRig &rig)
{
  ManipulatorState current = rig.current;
  if (const auto *c = node.child("current")) {
    if (!nodeToManipulatorState(*c, current))
      return false;
  }
  std::vector<CameraKeyframe> keyframes;
  if (!readNodeList(node, "keyframes", keyframes, nodeToKeyframe))
    return false;

  rig.current = current;
  rig.keyframes = std::move(keyframes);
  sortKeyframes(rig);
  return true;
}

bool saveCameraRigArchiveFile(
    const CameraRig &rig, const std::filesystem::path &file, std::string *error)
{
  vsr::core::DataTree tree;
  auto &root = tree.root();
  vsr::core::writeDataTreeMetadata(root,
      {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
          CAMERA_RIG_FILE_TYPE,
          CAMERA_RIG_SCHEMA,
          RIG_SCHEMA_VERSION});
  root["name"] = rig.name;
  cameraRigToNode(rig, root["rig"]);

  if (!tree.save(file.string().c_str())) {
    if (error)
      *error = "failed to write Camera Rig Archive";
    return false;
  }
  return true;
}

bool loadCameraRigArchiveFile(
    const std::filesystem::path &file, CameraRig &rigOut, std::string *error)
{
  vsr::core::DataTree tree;
  if (!tree.load(file.string().c_str())) {
    if (error)
      *error = "failed to load Camera Rig Archive";
    return false;
  }

  auto &root = tree.root();
  auto metadata = vsr::core::readDataTreeMetadata(root);
  if (metadata.malformed()) {
    if (error)
      *error = "malformed __vsr_metadata: " + metadata.message;
    return false;
  }
  if (!metadata.found()) {
    if (error)
      *error = "Camera Rig Archive is missing __vsr_metadata";
    return false;
  }

  const auto &m = *metadata.metadata;
  if (m.envelopeVersion != vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION) {
    if (error)
      *error = "unsupported metadata envelopeVersion";
    return false;
  }
  if (m.fileType != CAMERA_RIG_FILE_TYPE || m.schema != CAMERA_RIG_SCHEMA) {
    if (error)
      *error = "Archive is not a SciVis Studio Camera Rig Archive";
    return false;
  }
  if (m.schemaVersion < 1 || m.schemaVersion > RIG_SCHEMA_VERSION) {
    if (error)
      *error = "unsupported camera rig schemaVersion";
    return false;
  }

  CameraRig rig;
  rig.name = root["name"].getValueOr<std::string>("");
  if (const auto *rigNode = root.child("rig")) {
    if (!nodeToCameraRig(*rigNode, rig)) {
      if (error)
        *error = "Camera Rig Archive holds malformed rig data";
      return false;
    }
  }
  rigOut = std::move(rig);
  return true;
}

} // namespace vsr::scivis_studio::camera_rig
