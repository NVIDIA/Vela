// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Shot.h"
#include "DataNodeFields.h"

#include <algorithm>
#include <type_traits>

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

// Each patch's scalar fields, spelled once: f(wireName, patchField,
// &Struct::member) per field, so the codecs, the emptiness test and
// applyPatch all walk the same list. `P` may be const.
template <typename P, typename F>
void forEachField(P &p, F &&f)
{
  if constexpr (std::is_same_v<std::remove_const_t<P>,
                    ShotRenderSettingsPatch>) {
    using S = ShotRenderSettings;
    f("width", p.width, &S::width);
    f("height", p.height, &S::height);
    f("samples", p.samples, &S::samples);
    f("rendererLibrary", p.rendererLibrary, &S::rendererLibrary);
    f("rendererObjectIndex", p.rendererObjectIndex, &S::rendererObjectIndex);
    f("rendererSubtype", p.rendererSubtype, &S::rendererSubtype);
    f("outputFilePrefix", p.outputFilePrefix, &S::outputFilePrefix);
  } else {
    f("name", p.name, &Shot::name);
    f("frameCount", p.frameCount, &Shot::frameCount);
    f("fps", p.fps, &Shot::fps);
    f("currentFrame", p.currentFrame, &Shot::currentFrame);
    f("loop", p.loop, &Shot::loop);
    f("lightRigId", p.lightRigId, &Shot::lightRigId);
    f("cameraRigId", p.cameraRigId, &Shot::cameraRigId);
  }
}

template <typename P>
void patchToNode(const P &p, vsr::core::DataNode &n)
{
  forEachField(p, [&](const char *name, const auto &value, auto) {
    writeChild(n, name, value);
  });
}

template <typename P>
bool nodeToPatch(const vsr::core::DataNode &n, P &p)
{
  bool ok = true;
  forEachField(p, [&](const char *name, auto &value, auto) {
    ok = ok && readOptionalChild(n, name, value);
  });
  return ok;
}

bool isEmpty(const ShotRenderSettingsPatch &p)
{
  bool engaged = false;
  forEachField(p, [&](const char *, const auto &value, auto) {
    engaged = engaged || value.has_value();
  });
  return !engaged;
}

// Writes the patch's engaged scalar fields into `target`.
template <typename P, typename S>
void applyFields(const P &p, S &target)
{
  forEachField(p, [&](const char *, const auto &value, auto member) {
    if (value)
      target.*member = *value;
  });
}

} // namespace

void toNode(const ShotRenderSettingsPatch &p, vsr::core::DataNode &n)
{
  patchToNode(p, n);
}

bool fromNode(const vsr::core::DataNode &n, ShotRenderSettingsPatch &p)
{
  ShotRenderSettingsPatch out;
  if (!nodeToPatch(n, out))
    return false;
  p = std::move(out);
  return true;
}

void toNode(const ShotPatch &p, vsr::core::DataNode &n)
{
  patchToNode(p, n);
  if (!isEmpty(p.renderSettings))
    toNode(p.renderSettings, n["renderSettings"]);
  writeNodeList(n, "datasetBindings", p.datasetBindings);
}

bool fromNode(const vsr::core::DataNode &n, ShotPatch &p)
{
  ShotPatch out;
  if (!nodeToPatch(n, out)
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
  applyFields(patch, shot);
  applyFields(patch.renderSettings, shot.renderSettings);
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
