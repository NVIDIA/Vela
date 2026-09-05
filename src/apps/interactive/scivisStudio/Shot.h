// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Dataset.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::scivis_studio {

struct ShotRenderSettings
{
  uint32_t width{1024};
  uint32_t height{768};
  uint32_t samples{128};
  std::string rendererLibrary;
  size_t rendererObjectIndex{VSR_INVALID_INDEX};
  std::string rendererSubtype{"default"};
  std::string outputFilePrefix;
};

struct DatasetBinding
{
  DatasetID datasetId;
  bool enabled{true};
};

struct Shot
{
  ShotID id;
  std::string name;
  int frameCount{120};
  float fps{24.f};
  int currentFrame{0};
  bool playing{false};
  bool loop{true};
  std::vector<DatasetBinding> datasetBindings;
  LightRigID lightRigId;
  CameraRigID cameraRigId;
  SceneObjectRef camera;
  ShotRenderSettings renderSettings;
};

// An edit of some of a Shot's fields, the rest untouched: what one control
// commits (a Frames field, a Loop checkbox, a rig pick) and what UpdateShot
// carries, so two editors of the same shot never overwrite each other's
// fields. An engaged field replaces the shot's; datasetBindings sets the
// named datasets' bindings (shot::setDatasetBinding) and leaves the others.
// `id`, `playing` and `camera` have no patch field: identity, playback state
// (SetPlaying) and a runtime ref are not edits.
struct ShotRenderSettingsPatch
{
  std::optional<uint32_t> width;
  std::optional<uint32_t> height;
  std::optional<uint32_t> samples;
  std::optional<std::string> rendererLibrary;
  std::optional<size_t> rendererObjectIndex;
  std::optional<std::string> rendererSubtype;
  std::optional<std::string> outputFilePrefix;
};

struct ShotPatch
{
  std::optional<std::string> name;
  std::optional<int> frameCount;
  std::optional<float> fps;
  std::optional<int> currentFrame;
  std::optional<bool> loop;
  std::optional<LightRigID> lightRigId;
  std::optional<CameraRigID> cameraRigId;
  ShotRenderSettingsPatch renderSettings;
  std::vector<DatasetBinding> datasetBindings;
};

// Which fields of a model entity its DataNode carries. Manifest is what
// project.vsr stores: the persisted fields only, since the runtime-only ones
// (Shot::camera, Dataset::status, a rig's scene root, ...) are rebuilt when
// the project opens. Full is every field, for a receiver that cannot rebuild
// them: the Project Snapshot on the wire. Shot's codec below and
// projectToNode() (ProjectSerialization.h) take the same choice.
enum class ProjectForm
{
  Manifest,
  Full
};

// Shot's one serializer, for the manifest and the wire alike. Every persisted
// field is written, datasetBindings as an ordered list of {datasetId,
// enabled}; Full adds the runtime-only camera ref. The default form is the
// wire form, the one a receiver that cannot rebuild the runtime fields
// needs. On read `id` is required and a binding's datasetId is
// required; an absent optional child keeps the struct's default (name falls
// back to id) and camera is read when present; a mistyped child or unknown
// camera type is rejected and `shot` is left untouched.
void toNode(const Shot &shot,
    vsr::core::DataNode &node,
    ProjectForm form = ProjectForm::Full);
bool fromNode(const vsr::core::DataNode &node, Shot &shot);

// Every field, all optional on read with the struct's defaults.
void toNode(const ShotRenderSettings &settings, vsr::core::DataNode &node);
bool fromNode(const vsr::core::DataNode &node, ShotRenderSettings &settings);

// {datasetId (required), enabled (optional, default true)}.
void toNode(const DatasetBinding &binding, vsr::core::DataNode &node);
bool fromNode(const vsr::core::DataNode &node, DatasetBinding &binding);

// A patch writes exactly its engaged fields under the Shot's names
// (renderSettings as a nested node with its own engaged fields, bindings as
// a "0", "1", ... list), so an empty patch writes nothing; a present field
// reads as engaged and a mistyped one is rejected with `patch` untouched.
void toNode(const ShotRenderSettingsPatch &patch, vsr::core::DataNode &node);
bool fromNode(const vsr::core::DataNode &node, ShotRenderSettingsPatch &patch);
void toNode(const ShotPatch &patch, vsr::core::DataNode &node);
bool fromNode(const vsr::core::DataNode &node, ShotPatch &patch);

namespace shot {

DatasetBinding *findDatasetBinding(Shot &shot, const DatasetID &id);
const DatasetBinding *findDatasetBinding(const Shot &shot, const DatasetID &id);
void setDatasetBinding(Shot &shot, const DatasetID &id, bool enabled);
// Writes the patch's engaged fields into `shot` and sets its listed bindings.
// Applies no validation: updateShot (ShotOps.h) does that on the result.
void applyPatch(Shot &shot, const ShotPatch &patch);
// Brings a shot the user edited back inside its limits: at least one frame,
// the current frame within them, fps and the render size and sample count at
// least 1. What updateShot applies to every edit, whole Shot or patch.
void clampToValidRanges(Shot &shot);

} // namespace shot

} // namespace vsr::scivis_studio
