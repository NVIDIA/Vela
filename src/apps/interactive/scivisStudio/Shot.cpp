// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Shot.h"
#include "DataNodeFields.h"

#include <algorithm>

namespace vsr::scivis_studio {

// Serialization //////////////////////////////////////////////////////////////

void toNode(const DatasetBinding &binding, vsr::core::DataNode &node)
{
  writeChild(node, "datasetId", binding.datasetId);
  writeChild(node, "enabled", binding.enabled);
}

bool fromNode(const vsr::core::DataNode &node, DatasetBinding &binding)
{
  DatasetBinding out;
  if (!readChild(node, "datasetId", out.datasetId)
      || !readOptionalChild(node, "enabled", out.enabled))
    return false;
  binding = std::move(out);
  return true;
}

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

void toNode(const Shot &s, vsr::core::DataNode &n, ProjectForm form)
{
  writeChild(n, "id", s.id);
  writeChild(n, "name", s.name);
  writeChild(n, "frameCount", s.frameCount);
  writeChild(n, "fps", s.fps);
  writeChild(n, "currentFrame", s.currentFrame);
  writeChild(n, "playing", s.playing);
  writeChild(n, "loop", s.loop);
  writeChild(n, "lightRigId", s.lightRigId);
  writeChild(n, "cameraRigId", s.cameraRigId);
  if (form == ProjectForm::Full)
    writeChildNode(n, "camera", s.camera);
  toNode(s.renderSettings, n["renderSettings"]);
  writeAppendedList(n,
      "datasetBindings",
      s.datasetBindings,
      [](const DatasetBinding &b, vsr::core::DataNode &node) {
        toNode(b, node);
      });
}

bool fromNode(const vsr::core::DataNode &n, Shot &s)
{
  Shot out;
  if (!readChild(n, "id", out.id))
    return false;
  out.name = out.id;
  if (!readOptionalChild(n, "name", out.name)
      || !readOptionalChild(n, "frameCount", out.frameCount)
      || !readOptionalChild(n, "fps", out.fps)
      || !readOptionalChild(n, "currentFrame", out.currentFrame)
      || !readOptionalChild(n, "playing", out.playing)
      || !readOptionalChild(n, "loop", out.loop)
      || !readOptionalChild(n, "lightRigId", out.lightRigId)
      || !readOptionalChild(n, "cameraRigId", out.cameraRigId)
      || !readOptionalChildNode(n, "camera", out.camera)
      || !readOptionalChildNode(n, "renderSettings", out.renderSettings)
      || !readNodeList(n, "datasetBindings", out.datasetBindings))
    return false;
  s = std::move(out);
  return true;
}

// Patches ////////////////////////////////////////////////////////////////////

namespace {

bool isEmpty(const ShotRenderSettingsPatch &p)
{
  return !p.width && !p.height && !p.samples && !p.rendererLibrary
      && !p.rendererObjectIndex && !p.rendererSubtype && !p.outputFilePrefix;
}

// `field = value` when the patch has one.
template <typename T>
void patchField(T &field, const std::optional<T> &value)
{
  if (value)
    field = *value;
}

} // namespace

void toNode(const ShotRenderSettingsPatch &p, vsr::core::DataNode &n)
{
  writeChild(n, "width", p.width);
  writeChild(n, "height", p.height);
  writeChild(n, "samples", p.samples);
  writeChild(n, "rendererLibrary", p.rendererLibrary);
  writeChild(n, "rendererObjectIndex", p.rendererObjectIndex);
  writeChild(n, "rendererSubtype", p.rendererSubtype);
  writeChild(n, "outputFilePrefix", p.outputFilePrefix);
}

bool fromNode(const vsr::core::DataNode &n, ShotRenderSettingsPatch &p)
{
  ShotRenderSettingsPatch out;
  if (!readOptionalChild(n, "width", out.width)
      || !readOptionalChild(n, "height", out.height)
      || !readOptionalChild(n, "samples", out.samples)
      || !readOptionalChild(n, "rendererLibrary", out.rendererLibrary)
      || !readOptionalChild(n, "rendererObjectIndex", out.rendererObjectIndex)
      || !readOptionalChild(n, "rendererSubtype", out.rendererSubtype)
      || !readOptionalChild(n, "outputFilePrefix", out.outputFilePrefix))
    return false;
  p = std::move(out);
  return true;
}

void toNode(const ShotPatch &p, vsr::core::DataNode &n)
{
  writeChild(n, "name", p.name);
  writeChild(n, "frameCount", p.frameCount);
  writeChild(n, "fps", p.fps);
  writeChild(n, "currentFrame", p.currentFrame);
  writeChild(n, "loop", p.loop);
  writeChild(n, "lightRigId", p.lightRigId);
  writeChild(n, "cameraRigId", p.cameraRigId);
  if (!isEmpty(p.renderSettings))
    toNode(p.renderSettings, n["renderSettings"]);
  writeNodeList(n, "datasetBindings", p.datasetBindings);
}

bool fromNode(const vsr::core::DataNode &n, ShotPatch &p)
{
  ShotPatch out;
  if (!readOptionalChild(n, "name", out.name)
      || !readOptionalChild(n, "frameCount", out.frameCount)
      || !readOptionalChild(n, "fps", out.fps)
      || !readOptionalChild(n, "currentFrame", out.currentFrame)
      || !readOptionalChild(n, "loop", out.loop)
      || !readOptionalChild(n, "lightRigId", out.lightRigId)
      || !readOptionalChild(n, "cameraRigId", out.cameraRigId)
      || !readOptionalChildNode(n, "renderSettings", out.renderSettings)
      || !readNodeList(n, "datasetBindings", out.datasetBindings))
    return false;
  p = std::move(out);
  return true;
}

} // namespace vsr::scivis_studio

namespace vsr::scivis_studio::shot {

DatasetBinding *findDatasetBinding(Shot &shot, const DatasetID &id)
{
  auto itr = std::find_if(shot.datasetBindings.begin(),
      shot.datasetBindings.end(),
      [&](const DatasetBinding &b) { return b.datasetId == id; });
  return itr == shot.datasetBindings.end() ? nullptr : &*itr;
}

const DatasetBinding *findDatasetBinding(const Shot &shot, const DatasetID &id)
{
  auto itr = std::find_if(shot.datasetBindings.begin(),
      shot.datasetBindings.end(),
      [&](const DatasetBinding &b) { return b.datasetId == id; });
  return itr == shot.datasetBindings.end() ? nullptr : &*itr;
}

void setDatasetBinding(Shot &shot, const DatasetID &id, bool enabled)
{
  if (auto *binding = findDatasetBinding(shot, id)) {
    binding->enabled = enabled;
    return;
  }

  shot.datasetBindings.push_back({id, enabled});
}

void applyPatch(Shot &shot, const ShotPatch &patch)
{
  patchField(shot.name, patch.name);
  patchField(shot.frameCount, patch.frameCount);
  patchField(shot.fps, patch.fps);
  patchField(shot.currentFrame, patch.currentFrame);
  patchField(shot.loop, patch.loop);
  patchField(shot.lightRigId, patch.lightRigId);
  patchField(shot.cameraRigId, patch.cameraRigId);
  auto &r = shot.renderSettings;
  const auto &p = patch.renderSettings;
  patchField(r.width, p.width);
  patchField(r.height, p.height);
  patchField(r.samples, p.samples);
  patchField(r.rendererLibrary, p.rendererLibrary);
  patchField(r.rendererObjectIndex, p.rendererObjectIndex);
  patchField(r.rendererSubtype, p.rendererSubtype);
  patchField(r.outputFilePrefix, p.outputFilePrefix);
  for (const auto &binding : patch.datasetBindings)
    setDatasetBinding(shot, binding.datasetId, binding.enabled);
}

void clampToValidRanges(Shot &shot)
{
  shot.frameCount = std::max(1, shot.frameCount);
  shot.currentFrame = std::clamp(shot.currentFrame, 0, shot.frameCount - 1);
  shot.fps = std::max(1.f, shot.fps);
  shot.renderSettings.width = std::max(1u, shot.renderSettings.width);
  shot.renderSettings.height = std::max(1u, shot.renderSettings.height);
  shot.renderSettings.samples = std::max(1u, shot.renderSettings.samples);
}

} // namespace vsr::scivis_studio::shot
