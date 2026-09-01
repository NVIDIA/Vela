// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Dataset.h"
#include "Shot.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <filesystem>
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Client->server requests for shots, light/camera rigs and color maps, plus
 * the result payloads their ProjectOpReply carries. Every request is a sync
 * project op that starts with a requestId; entities are addressed by their
 * string ids, never by position.
 *
 * Example:
 *   UpdateShot req;
 *   req.requestId = 7;
 *   req.shot = project.shots[0];
 *   send(encode(req));
 */

// Shot ///////////////////////////////////////////////////////////////////////

struct CreateShot
{
  static constexpr StudioMessageType kType = StudioMessageType::CreateShot;
  uint64_t requestId{0};
  std::string name;
};

struct RemoveShot
{
  static constexpr StudioMessageType kType = StudioMessageType::RemoveShot;
  uint64_t requestId{0};
  vsr::scivis_studio::ShotID shotId;
};

// The whole Shot; the server validates and replaces its copy.
struct UpdateShot
{
  static constexpr StudioMessageType kType = StudioMessageType::UpdateShot;
  uint64_t requestId{0};
  vsr::scivis_studio::Shot shot;
};

struct SetActiveShot
{
  static constexpr StudioMessageType kType = StudioMessageType::SetActiveShot;
  uint64_t requestId{0};
  vsr::scivis_studio::ShotID shotId;
};

struct ShotCreatedResult
{
  vsr::scivis_studio::ShotID shotId;
};

// Light rig //////////////////////////////////////////////////////////////////

struct CreateLightRig
{
  static constexpr StudioMessageType kType = StudioMessageType::CreateLightRig;
  uint64_t requestId{0};
  std::string name;
};

struct CloneLightRig
{
  static constexpr StudioMessageType kType = StudioMessageType::CloneLightRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
};

struct RemoveLightRig
{
  static constexpr StudioMessageType kType = StudioMessageType::RemoveLightRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
};

struct RenameLightRig
{
  static constexpr StudioMessageType kType = StudioMessageType::RenameLightRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  std::string newName;
};

// subtype is the ANARI light subtype ("directional", "point", ...).
struct AddLightToRig
{
  static constexpr StudioMessageType kType = StudioMessageType::AddLightToRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  std::string subtype;
};

struct RemoveLightFromRig
{
  static constexpr StudioMessageType kType =
      StudioMessageType::RemoveLightFromRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  vsr::scivis_studio::SceneNodeRef lightNode;
};

struct SaveLightRigArchive
{
  static constexpr StudioMessageType kType =
      StudioMessageType::SaveLightRigArchive;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  std::filesystem::path file;
};

struct LoadLightRigArchive
{
  static constexpr StudioMessageType kType =
      StudioMessageType::LoadLightRigArchive;
  uint64_t requestId{0};
  std::filesystem::path file;
};

// Reply to CreateLightRig, CloneLightRig and LoadLightRigArchive.
struct LightRigCreatedResult
{
  vsr::scivis_studio::LightRigID lightRigId;
};

// Reply to AddLightToRig.
struct LightAddedResult
{
  vsr::scivis_studio::SceneNodeRef lightNode;
};

// Camera rig /////////////////////////////////////////////////////////////////

struct CreateCameraRig
{
  static constexpr StudioMessageType kType = StudioMessageType::CreateCameraRig;
  uint64_t requestId{0};
  std::string name;
};

struct RemoveCameraRig
{
  static constexpr StudioMessageType kType = StudioMessageType::RemoveCameraRig;
  uint64_t requestId{0};
  vsr::scivis_studio::CameraRigID cameraRigId;
};

struct RenameCameraRig
{
  static constexpr StudioMessageType kType = StudioMessageType::RenameCameraRig;
  uint64_t requestId{0};
  vsr::scivis_studio::CameraRigID cameraRigId;
  std::string newName;
};

struct SaveCameraRigArchive
{
  static constexpr StudioMessageType kType =
      StudioMessageType::SaveCameraRigArchive;
  uint64_t requestId{0};
  vsr::scivis_studio::CameraRigID cameraRigId;
  std::filesystem::path file;
};

struct LoadCameraRigArchive
{
  static constexpr StudioMessageType kType =
      StudioMessageType::LoadCameraRigArchive;
  uint64_t requestId{0};
  std::filesystem::path file;
};

// Reply to CreateCameraRig and LoadCameraRigArchive.
struct CameraRigCreatedResult
{
  vsr::scivis_studio::CameraRigID cameraRigId;
};

// Color map //////////////////////////////////////////////////////////////////

struct CreateColorMap
{
  static constexpr StudioMessageType kType = StudioMessageType::CreateColorMap;
  uint64_t requestId{0};
  std::string name;
};

struct RenameColorMap
{
  static constexpr StudioMessageType kType = StudioMessageType::RenameColorMap;
  uint64_t requestId{0};
  vsr::scivis_studio::ColorMapID colorMapId;
  std::string newName;
};

struct RemoveColorMap
{
  static constexpr StudioMessageType kType = StudioMessageType::RemoveColorMap;
  uint64_t requestId{0};
  vsr::scivis_studio::ColorMapID colorMapId;
};

// The server creates both halves atomically: the ColorMapRecord and the
// scene-side object the record names.
struct ColorMapCreatedResult
{
  vsr::scivis_studio::ColorMapID colorMapId;
  vsr::scivis_studio::SceneObjectRef object;
};

// Shot serialization /////////////////////////////////////////////////////////

// The standalone Shot form (ProjectSerialization only exposes the whole
// Project). Every field travels, including the runtime-only camera ref;
// datasetBindings are keyed by datasetId ("datasetBindings/<id>/enabled").
// On read `id` is required; absent optional children keep the struct's
// defaults, a mistyped child is rejected.
void toNode(const vsr::scivis_studio::Shot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, vsr::scivis_studio::Shot &);

void toNode(
    const vsr::scivis_studio::ShotRenderSettings &, vsr::core::DataNode &);
bool fromNode(
    const vsr::core::DataNode &, vsr::scivis_studio::ShotRenderSettings &);

// Requests ///////////////////////////////////////////////////////////////////

// requestId and the id/name/path fields are required; name may be "".

void toNode(const CreateShot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, CreateShot &);
void toNode(const RemoveShot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveShot &);
void toNode(const UpdateShot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, UpdateShot &);
void toNode(const SetActiveShot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetActiveShot &);

void toNode(const CreateLightRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, CreateLightRig &);
void toNode(const CloneLightRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, CloneLightRig &);
void toNode(const RemoveLightRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveLightRig &);
void toNode(const RenameLightRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RenameLightRig &);
void toNode(const AddLightToRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, AddLightToRig &);
void toNode(const RemoveLightFromRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveLightFromRig &);
void toNode(const SaveLightRigArchive &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SaveLightRigArchive &);
void toNode(const LoadLightRigArchive &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, LoadLightRigArchive &);

void toNode(const CreateCameraRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, CreateCameraRig &);
void toNode(const RemoveCameraRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveCameraRig &);
void toNode(const RenameCameraRig &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RenameCameraRig &);
void toNode(const SaveCameraRigArchive &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SaveCameraRigArchive &);
void toNode(const LoadCameraRigArchive &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, LoadCameraRigArchive &);

void toNode(const CreateColorMap &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, CreateColorMap &);
void toNode(const RenameColorMap &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RenameColorMap &);
void toNode(const RemoveColorMap &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveColorMap &);

// Results ////////////////////////////////////////////////////////////////////

void toNode(const ShotCreatedResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ShotCreatedResult &);
void toNode(const LightRigCreatedResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, LightRigCreatedResult &);
void toNode(const LightAddedResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, LightAddedResult &);
void toNode(const CameraRigCreatedResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, CameraRigCreatedResult &);
void toNode(const ColorMapCreatedResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ColorMapCreatedResult &);

} // namespace vsr::scivis_studio::protocol
