// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Shot.h"
#include "DataNodeFields.h"

#include <algorithm>

namespace vsr::scivis_studio {

// Serialization //////////////////////////////////////////////////////////////

namespace {

void bindingToNode(const DatasetBinding &binding, vsr::core::DataNode &node)
{
  writeChild(node, "datasetId", binding.datasetId);
  writeChild(node, "enabled", binding.enabled);
}

bool nodeToBinding(const vsr::core::DataNode &node, DatasetBinding &binding)
{
  return readChild(node, "datasetId", binding.datasetId)
      && readOptionalChild(node, "enabled", binding.enabled);
}

} // namespace

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
  writeAppendedList(n, "datasetBindings", s.datasetBindings, bindingToNode);
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
      || !readNodeList(
          n, "datasetBindings", out.datasetBindings, nodeToBinding))
    return false;
  s = std::move(out);
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
