// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ReplicaView.h"
// std
#include <algorithm>
#include <cctype>

namespace vsr::scivis_studio::client::replica {

namespace {

bool nameLess(const std::string &a, const std::string &b)
{
  const auto lower = [](unsigned char c) { return std::tolower(c); };
  const auto result = std::lexicographical_compare(a.begin(),
      a.end(),
      b.begin(),
      b.end(),
      [&](unsigned char x, unsigned char y) { return lower(x) < lower(y); });
  return result;
}

template <typename T>
std::vector<const T *> sortedByName(const std::vector<T> &items)
{
  std::vector<const T *> out;
  out.reserve(items.size());
  for (const T &item : items)
    out.push_back(&item);
  std::stable_sort(out.begin(), out.end(), [](const T *a, const T *b) {
    if (nameLess(a->name, b->name))
      return true;
    if (nameLess(b->name, a->name))
      return false;
    return a->id < b->id;
  });
  return out;
}

template <typename Entity>
std::string label(const Entity *entity, const std::string &id)
{
  if (id.empty())
    return "<none>";
  if (!entity)
    return "<missing: " + id + ">";
  return entity->name;
}

} // namespace

// Lookups ////////////////////////////////////////////////////////////////////

const Dataset *findDataset(const Project &project, const DatasetID &id)
{
  return project::findDataset(project, id);
}

const Shot *findShot(const Project &project, const ShotID &id)
{
  return project::findShot(project, id);
}

const Shot *activeShot(const Project &project)
{
  return project::activeShot(project);
}

const LightRig *findLightRig(const Project &project, const LightRigID &id)
{
  return light_rig::findLightRig(project, id);
}

const CameraRig *findCameraRig(const Project &project, const CameraRigID &id)
{
  return camera_rig::findCameraRig(project, id);
}

const ColorMapRecord *findColorMap(const Project &project, const ColorMapID &id)
{
  return project::findColorMap(project, id);
}

size_t lightRigUseCount(const Project &project, const LightRigID &id)
{
  return size_t(std::count_if(project.shots.begin(),
      project.shots.end(),
      [&](const Shot &shot) { return shot.lightRigId == id; }));
}

size_t cameraRigUseCount(const Project &project, const CameraRigID &id)
{
  return size_t(std::count_if(project.shots.begin(),
      project.shots.end(),
      [&](const Shot &shot) { return shot.cameraRigId == id; }));
}

// Display strings ////////////////////////////////////////////////////////////

const char *datasetStatusText(const Dataset &dataset)
{
  return dataset::displayStatus(dataset);
}

const char *datasetSourceKindText(const Dataset &dataset)
{
  return dataset::toString(dataset.sourceKind);
}

const char *datasetResidencyText(const Dataset &dataset)
{
  return dataset::toString(dataset.residency);
}

std::string projectDirectoryText(const Project &project)
{
  if (project.projectDirectory.empty())
    return "{unsaved}";
  return project.projectDirectory.generic_string();
}

std::string datasetLabel(const Project &project, const DatasetID &id)
{
  return label(findDataset(project, id), id);
}

std::string shotLabel(const Project &project, const ShotID &id)
{
  return label(findShot(project, id), id);
}

std::string lightRigLabel(const Project &project, const LightRigID &id)
{
  return label(findLightRig(project, id), id);
}

std::string cameraRigLabel(const Project &project, const CameraRigID &id)
{
  return label(findCameraRig(project, id), id);
}

std::string colorMapLabel(const Project &project, const ColorMapID &id)
{
  return label(findColorMap(project, id), id);
}

// Sorted views ///////////////////////////////////////////////////////////////

std::vector<const Dataset *> sortedDatasets(const Project &project)
{
  return sortedByName(project.datasets);
}

std::vector<const Shot *> sortedShots(const Project &project)
{
  return sortedByName(project.shots);
}

std::vector<const LightRig *> sortedLightRigs(const Project &project)
{
  return sortedByName(project.lightRigs);
}

std::vector<const CameraRig *> sortedCameraRigs(const Project &project)
{
  return sortedByName(project.cameraRigs);
}

std::vector<const ColorMapRecord *> sortedColorMaps(const Project &project)
{
  return sortedByName(project.colorMaps);
}

} // namespace vsr::scivis_studio::client::replica
