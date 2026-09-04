// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ShotRigRequests.h"
#include "PayloadCommon.h"

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

// ShotRenderSettings and Shot are model types with hand-written codecs, so
// PayloadCommon.h's nested-child templates cannot see them (they are found
// by ADL only for protocol types); these two read them by hand.
bool readOptionalRenderSettings(
    const vsr::core::DataNode &parent, ShotRenderSettings &out)
{
  const auto *c = parent.child("renderSettings");
  return !c || fromNode(*c, out);
}

bool readShot(const vsr::core::DataNode &parent, Shot &out)
{
  const auto *c = parent.child("shot");
  return c && fromNode(*c, out);
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
  toNode(s.renderSettings, n["renderSettings"]);
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
      || !readOptionalRenderSettings(n, out.renderSettings))
    return false;
  s = std::move(out);
  return true;
}

// UpdateShot //////////////////////////////////////////////////////////////////

void toNode(const UpdateShot &p, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", p.requestId);
  toNode(p.shot, n["shot"]);
}

bool fromNode(const vsr::core::DataNode &n, UpdateShot &p)
{
  return readChild(n, "requestId", p.requestId) && readShot(n, p.shot);
}

} // namespace vsr::scivis_studio::protocol
