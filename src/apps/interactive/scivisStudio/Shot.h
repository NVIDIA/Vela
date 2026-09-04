// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Dataset.h"

#include <cstdint>
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

// Which fields of a model entity its DataNode carries. Manifest is what
// project.vsr stores: the persisted fields only, since the runtime-only ones
// (Shot::camera, Dataset::status, a rig's scene root, ...) are rebuilt when
// the project opens. Full is every field, for a receiver that cannot rebuild
// them: the Project Snapshot and UpdateShot on the wire. Shot's codec below
// and projectToNode() (ProjectSerialization.h) take the same choice.
enum class ProjectForm
{
  Manifest,
  Full
};

// Shot's one serializer, for the manifest and the wire alike. Every persisted
// field is written, datasetBindings as an ordered list of {datasetId,
// enabled}; Full adds the runtime-only camera ref. The default form is the
// wire form so a payload nesting a Shot reaches it as a plain
// toNode(value, node). On read `id` is required and a binding's datasetId is
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

namespace shot {

DatasetBinding *findDatasetBinding(Shot &shot, const DatasetID &id);
const DatasetBinding *findDatasetBinding(const Shot &shot, const DatasetID &id);
void setDatasetBinding(Shot &shot, const DatasetID &id, bool enabled);
// Brings a shot the user edited back inside its limits: at least one frame,
// the current frame within them, fps and the render size and sample count at
// least 1. What updateShot applies; a client editing a draft uses the same.
void clampToValidRanges(Shot &shot);

} // namespace shot

} // namespace vsr::scivis_studio
