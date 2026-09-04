// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
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
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::CreateShot;
  uint64_t requestId{0};
  std::string name;
};

struct RemoveShot
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveShot;
  uint64_t requestId{0};
  vsr::scivis_studio::ShotID shotId;
};

// The whole Shot; the server validates and replaces its copy.
struct UpdateShot
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::UpdateShot;
  uint64_t requestId{0};
  vsr::scivis_studio::Shot shot;
};

struct SetActiveShot
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetActiveShot;
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
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::CreateLightRig;
  uint64_t requestId{0};
  std::string name;
};

struct CloneLightRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::CloneLightRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
};

struct RemoveLightRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveLightRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
};

struct RenameLightRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RenameLightRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  std::string newName;
};

// subtype is the ANARI light subtype ("directional", "point", ...).
struct AddLightToRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::AddLightToRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  std::string subtype;
};

struct RemoveLightFromRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveLightFromRig;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  vsr::scivis_studio::SceneNodeRef lightNode;
};

struct SaveLightRigArchive
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SaveLightRigArchive;
  uint64_t requestId{0};
  vsr::scivis_studio::LightRigID lightRigId;
  std::filesystem::path file;
};

struct LoadLightRigArchive
{
  static constexpr StudioMessageType MESSAGE_TYPE =
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
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::CreateCameraRig;
  uint64_t requestId{0};
  std::string name;
};

struct RemoveCameraRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveCameraRig;
  uint64_t requestId{0};
  vsr::scivis_studio::CameraRigID cameraRigId;
};

struct RenameCameraRig
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RenameCameraRig;
  uint64_t requestId{0};
  vsr::scivis_studio::CameraRigID cameraRigId;
  std::string newName;
};

struct SaveCameraRigArchive
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SaveCameraRigArchive;
  uint64_t requestId{0};
  vsr::scivis_studio::CameraRigID cameraRigId;
  std::filesystem::path file;
};

struct LoadCameraRigArchive
{
  static constexpr StudioMessageType MESSAGE_TYPE =
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
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::CreateColorMap;
  uint64_t requestId{0};
  std::string name;
};

struct RenameColorMap
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RenameColorMap;
  uint64_t requestId{0};
  vsr::scivis_studio::ColorMapID colorMapId;
  std::string newName;
};

struct RemoveColorMap
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveColorMap;
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

// Every payload is a fields() description (PayloadCommon.h): requestId and
// the id, name, path and nested-ref fields are all required; name may be "".
// UpdateShot nests the model's Shot in its Full form (Shot.h): every field
// including the runtime camera ref, bindings as the manifest's ordered list.

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, CreateShot &p)
{
  v.required("requestId", p.requestId);
  v.required("name", p.name);
}

template <typename V>
void fields(V &v, RemoveShot &p)
{
  v.required("requestId", p.requestId);
  v.required("shotId", p.shotId);
}

template <typename V>
void fields(V &v, UpdateShot &p)
{
  v.required("requestId", p.requestId);
  v.child("shot", p.shot);
}

template <typename V>
void fields(V &v, SetActiveShot &p)
{
  v.required("requestId", p.requestId);
  v.required("shotId", p.shotId);
}

template <typename V>
void fields(V &v, CreateLightRig &p)
{
  v.required("requestId", p.requestId);
  v.required("name", p.name);
}

template <typename V>
void fields(V &v, CloneLightRig &p)
{
  v.required("requestId", p.requestId);
  v.required("lightRigId", p.lightRigId);
}

template <typename V>
void fields(V &v, RemoveLightRig &p)
{
  v.required("requestId", p.requestId);
  v.required("lightRigId", p.lightRigId);
}

template <typename V>
void fields(V &v, RenameLightRig &p)
{
  v.required("requestId", p.requestId);
  v.required("lightRigId", p.lightRigId);
  v.required("newName", p.newName);
}

template <typename V>
void fields(V &v, AddLightToRig &p)
{
  v.required("requestId", p.requestId);
  v.required("lightRigId", p.lightRigId);
  v.required("subtype", p.subtype);
}

template <typename V>
void fields(V &v, RemoveLightFromRig &p)
{
  v.required("requestId", p.requestId);
  v.required("lightRigId", p.lightRigId);
  v.child("lightNode", p.lightNode);
}

template <typename V>
void fields(V &v, SaveLightRigArchive &p)
{
  v.required("requestId", p.requestId);
  v.required("lightRigId", p.lightRigId);
  v.required("file", p.file);
}

template <typename V>
void fields(V &v, LoadLightRigArchive &p)
{
  v.required("requestId", p.requestId);
  v.required("file", p.file);
}

template <typename V>
void fields(V &v, CreateCameraRig &p)
{
  v.required("requestId", p.requestId);
  v.required("name", p.name);
}

template <typename V>
void fields(V &v, RemoveCameraRig &p)
{
  v.required("requestId", p.requestId);
  v.required("cameraRigId", p.cameraRigId);
}

template <typename V>
void fields(V &v, RenameCameraRig &p)
{
  v.required("requestId", p.requestId);
  v.required("cameraRigId", p.cameraRigId);
  v.required("newName", p.newName);
}

template <typename V>
void fields(V &v, SaveCameraRigArchive &p)
{
  v.required("requestId", p.requestId);
  v.required("cameraRigId", p.cameraRigId);
  v.required("file", p.file);
}

template <typename V>
void fields(V &v, LoadCameraRigArchive &p)
{
  v.required("requestId", p.requestId);
  v.required("file", p.file);
}

template <typename V>
void fields(V &v, CreateColorMap &p)
{
  v.required("requestId", p.requestId);
  v.required("name", p.name);
}

template <typename V>
void fields(V &v, RenameColorMap &p)
{
  v.required("requestId", p.requestId);
  v.required("colorMapId", p.colorMapId);
  v.required("newName", p.newName);
}

template <typename V>
void fields(V &v, RemoveColorMap &p)
{
  v.required("requestId", p.requestId);
  v.required("colorMapId", p.colorMapId);
}

template <typename V>
void fields(V &v, ShotCreatedResult &p)
{
  v.required("shotId", p.shotId);
}

template <typename V>
void fields(V &v, LightRigCreatedResult &p)
{
  v.required("lightRigId", p.lightRigId);
}

template <typename V>
void fields(V &v, LightAddedResult &p)
{
  v.child("lightNode", p.lightNode);
}

template <typename V>
void fields(V &v, CameraRigCreatedResult &p)
{
  v.required("cameraRigId", p.cameraRigId);
}

template <typename V>
void fields(V &v, ColorMapCreatedResult &p)
{
  v.required("colorMapId", p.colorMapId);
  v.child("object", p.object);
}

} // namespace vsr::scivis_studio::protocol
