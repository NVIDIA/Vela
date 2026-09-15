// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Dataset.h"

#include "vsr/core/DataTree.hpp"
#include "vsr/rendering/view/Manipulator.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio {

struct Project;

enum class CameraInterpolation
{
  Hold,
  Linear,
  EaseOut,
  EaseIn,
  EaseOutIn
};

struct ManipulatorState
{
  vsr::rendering::CameraPose orbit;
};

struct CameraKeyframe
{
  int frame{0};
  std::string name;
  ManipulatorState manipulator;
  CameraInterpolation interpolationToNext{CameraInterpolation::Linear};
};

struct CameraRig
{
  CameraRigID id;
  std::string name;
  ManipulatorState current;
  std::vector<CameraKeyframe> keyframes;

  // Runtime-only name of the asset path owned by this rig.
  std::string persistedName;
};

namespace camera_rig {

// Collection lookups within a Project.
CameraRigID nextCameraRigId(const Project &project);
CameraRig *findCameraRig(Project &project, const CameraRigID &id);
const CameraRig *findCameraRig(const Project &project, const CameraRigID &id);

// Interpolation enum <-> persisted string. The parser is strict: the mode
// whose toString() spelling is `s`, empty for anything else, so a corrupt rig
// is reported rather than read as Linear.
const char *toString(CameraInterpolation interpolation);
std::optional<CameraInterpolation> interpolationFromString(
    const std::string &s);

// Manipulator <-> stored camera pose.
ManipulatorState manipulatorStateFromManipulator(
    const vsr::rendering::Manipulator &m);
void applyManipulatorState(
    vsr::rendering::Manipulator &m, const ManipulatorState &state);

// Keyframe animation.
void sortKeyframes(CameraRig &rig);
ManipulatorState sampleCameraRig(const CameraRig &rig, int frame);

// Standalone Camera Rig Archive IO. The rig's runtime-only id is intentionally
// not written; only the portable name and value data (current pose + keyframes)
// are stored. loadCameraRigArchiveFile fills rigOut.name and value data,
// leaving rigOut.id empty for the caller to assign.
bool saveCameraRigArchiveFile(const CameraRig &rig,
    const std::filesystem::path &file,
    std::string *error = nullptr);
bool loadCameraRigArchiveFile(const std::filesystem::path &file,
    CameraRig &rigOut,
    std::string *error = nullptr);

// DataTree node <-> camera rig value data (current pose + keyframes); the
// Camera Rig Archive, the legacy (pre-v4) inline manifest and the Project
// Snapshot's Full form all carry it. Reading assigns rig.current and
// rig.keyframes only on success: an absent child keeps the rig as is, a
// mistyped pose or keyframe field or an unknown interpolation spelling is
// rejected.
void cameraRigToNode(const CameraRig &rig, vsr::core::DataNode &node);
bool nodeToCameraRig(const vsr::core::DataNode &node, CameraRig &rig);

} // namespace camera_rig

} // namespace vsr::scivis_studio
