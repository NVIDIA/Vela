// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ShotRigRequests.h"
#include "PayloadMacros.h"

namespace vsr::scivis_studio::protocol {

using vsr::scivis_studio::DatasetBinding;
using vsr::scivis_studio::Shot;
using vsr::scivis_studio::ShotRenderSettings;

namespace {

// Bindings are keyed by datasetId so a binding has a stable address
// (ADR 0025); an empty list writes nothing.
void writeDatasetBindings(
    vsr::core::DataNode &parent, const std::vector<DatasetBinding> &bindings)
{
  if (bindings.empty())
    return;
  auto &list = parent["datasetBindings"];
  for (const auto &b : bindings)
    writeChild(list[b.datasetId], "enabled", b.enabled);
}

bool readDatasetBindings(
    const vsr::core::DataNode &parent, std::vector<DatasetBinding> &out)
{
  out.clear();
  const auto *list = parent.child("datasetBindings");
  if (!list)
    return true;
  bool ok = true;
  list->foreach_child_const([&](const vsr::core::DataNode &b) {
    DatasetBinding binding;
    binding.datasetId = b.name();
    if (!readOptionalChild(b, "enabled", binding.enabled)) {
      ok = false;
      return;
    }
    out.push_back(std::move(binding));
  });
  return ok;
}

} // namespace

// Shot serialization /////////////////////////////////////////////////////////

void toNode(const ShotRenderSettings &r, vsr::core::DataNode &n)
{
  writeChild(n, "width", r.width);
  writeChild(n, "height", r.height);
  writeChild(n, "samples", r.samples);
  writeChild(n, "rendererLibrary", r.rendererLibrary);
  writeChild(n, "rendererObjectIndex", r.rendererObjectIndex);
  writeChild(n, "rendererSubtype", r.rendererSubtype);
  writeChild(n, "outputFilePrefix", r.outputFilePrefix);
}

bool fromNode(const vsr::core::DataNode &n, ShotRenderSettings &r)
{
  ShotRenderSettings out;
  if (!readOptionalChild(n, "width", out.width)
      || !readOptionalChild(n, "height", out.height)
      || !readOptionalChild(n, "samples", out.samples)
      || !readOptionalChild(n, "rendererLibrary", out.rendererLibrary)
      || !readOptionalChild(n, "rendererObjectIndex", out.rendererObjectIndex)
      || !readOptionalChild(n, "rendererSubtype", out.rendererSubtype)
      || !readOptionalChild(n, "outputFilePrefix", out.outputFilePrefix))
    return false;
  r = std::move(out);
  return true;
}

void toNode(const Shot &s, vsr::core::DataNode &n)
{
  writeChild(n, "id", s.id);
  writeChild(n, "name", s.name);
  writeChild(n, "frameCount", s.frameCount);
  writeChild(n, "fps", s.fps);
  writeChild(n, "currentFrame", s.currentFrame);
  writeChild(n, "playing", s.playing);
  writeChild(n, "loop", s.loop);
  writeDatasetBindings(n, s.datasetBindings);
  writeChild(n, "lightRigId", s.lightRigId);
  writeChild(n, "cameraRigId", s.cameraRigId);
  writeChildNode(n, "camera", s.camera);
  writeChildNode(n, "renderSettings", s.renderSettings);
}

bool fromNode(const vsr::core::DataNode &n, Shot &s)
{
  Shot out;
  if (!readChild(n, "id", out.id))
    return false;
  if (!readOptionalChild(n, "name", out.name)
      || !readOptionalChild(n, "frameCount", out.frameCount)
      || !readOptionalChild(n, "fps", out.fps)
      || !readOptionalChild(n, "currentFrame", out.currentFrame)
      || !readOptionalChild(n, "playing", out.playing)
      || !readOptionalChild(n, "loop", out.loop)
      || !readDatasetBindings(n, out.datasetBindings)
      || !readOptionalChild(n, "lightRigId", out.lightRigId)
      || !readOptionalChild(n, "cameraRigId", out.cameraRigId)
      || !readOptionalChildNode(n, "camera", out.camera)
      || !readOptionalChildNode(n, "renderSettings", out.renderSettings))
    return false;
  s = std::move(out);
  return true;
}

// Requests ///////////////////////////////////////////////////////////////////

// Most requests are {requestId, one id or name}; see PayloadMacros.h.

// Shot

VSR_STUDIO_ID_REQUEST(CreateShot, name)
VSR_STUDIO_ID_REQUEST(RemoveShot, shotId)
VSR_STUDIO_ID_REQUEST(SetActiveShot, shotId)

void toNode(const UpdateShot &p, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", p.requestId);
  writeChildNode(n, "shot", p.shot);
}

bool fromNode(const vsr::core::DataNode &n, UpdateShot &p)
{
  return readChild(n, "requestId", p.requestId)
      && readChildNode(n, "shot", p.shot);
}

// Light rig

VSR_STUDIO_ID_REQUEST(CreateLightRig, name)
VSR_STUDIO_ID_REQUEST(CloneLightRig, lightRigId)
VSR_STUDIO_ID_REQUEST(RemoveLightRig, lightRigId)
VSR_STUDIO_RENAME_REQUEST(RenameLightRig, lightRigId)
VSR_STUDIO_ARCHIVE_SAVE_REQUEST(SaveLightRigArchive, lightRigId)
VSR_STUDIO_ARCHIVE_LOAD_REQUEST(LoadLightRigArchive)

void toNode(const AddLightToRig &p, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", p.requestId);
  writeChild(n, "lightRigId", p.lightRigId);
  writeChild(n, "subtype", p.subtype);
}

bool fromNode(const vsr::core::DataNode &n, AddLightToRig &p)
{
  return readChild(n, "requestId", p.requestId)
      && readChild(n, "lightRigId", p.lightRigId)
      && readChild(n, "subtype", p.subtype);
}

void toNode(const RemoveLightFromRig &p, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", p.requestId);
  writeChild(n, "lightRigId", p.lightRigId);
  writeChildNode(n, "lightNode", p.lightNode);
}

bool fromNode(const vsr::core::DataNode &n, RemoveLightFromRig &p)
{
  return readChild(n, "requestId", p.requestId)
      && readChild(n, "lightRigId", p.lightRigId)
      && readChildNode(n, "lightNode", p.lightNode);
}

// Camera rig

VSR_STUDIO_ID_REQUEST(CreateCameraRig, name)
VSR_STUDIO_ID_REQUEST(RemoveCameraRig, cameraRigId)
VSR_STUDIO_RENAME_REQUEST(RenameCameraRig, cameraRigId)
VSR_STUDIO_ARCHIVE_SAVE_REQUEST(SaveCameraRigArchive, cameraRigId)
VSR_STUDIO_ARCHIVE_LOAD_REQUEST(LoadCameraRigArchive)

// Color map

VSR_STUDIO_ID_REQUEST(CreateColorMap, name)
VSR_STUDIO_RENAME_REQUEST(RenameColorMap, colorMapId)
VSR_STUDIO_ID_REQUEST(RemoveColorMap, colorMapId)

// Results ////////////////////////////////////////////////////////////////////

VSR_STUDIO_ID_RESULT(ShotCreatedResult, shotId)
VSR_STUDIO_ID_RESULT(LightRigCreatedResult, lightRigId)
VSR_STUDIO_ID_RESULT(CameraRigCreatedResult, cameraRigId)

void toNode(const LightAddedResult &p, vsr::core::DataNode &n)
{
  writeChildNode(n, "lightNode", p.lightNode);
}

bool fromNode(const vsr::core::DataNode &n, LightAddedResult &p)
{
  return readChildNode(n, "lightNode", p.lightNode);
}

void toNode(const ColorMapCreatedResult &p, vsr::core::DataNode &n)
{
  writeChild(n, "colorMapId", p.colorMapId);
  writeChildNode(n, "object", p.object);
}

bool fromNode(const vsr::core::DataNode &n, ColorMapCreatedResult &p)
{
  return readChild(n, "colorMapId", p.colorMapId)
      && readChildNode(n, "object", p.object);
}

} // namespace vsr::scivis_studio::protocol
