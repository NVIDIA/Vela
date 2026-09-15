// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Project.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace vsr::scivis_studio {

namespace {

bool nameLess(const std::string &a, const std::string &b)
{
  const auto lower = [](unsigned char c) { return std::tolower(c); };
  return std::lexicographical_compare(a.begin(),
      a.end(),
      b.begin(),
      b.end(),
      [&](unsigned char x, unsigned char y) { return lower(x) < lower(y); });
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

bool Project::isSaved() const
{
  return !projectDirectory.empty();
}

void Project::markDirty()
{
  dirty = true;
}

void Project::markClean()
{
  dirty = false;
}

namespace project {

std::string makeGeneratedId(const char *prefix, size_t ordinal)
{
  std::ostringstream ss;
  ss << prefix << '_' << std::setfill('0') << std::setw(4) << ordinal;
  return ss.str();
}

DatasetID nextDatasetId(Project &project)
{
  for (;;) {
    const auto ordinal = project.nextDatasetOrdinal++;
    const auto candidate = makeGeneratedId("dataset", ordinal);
    if (std::none_of(project.datasets.begin(),
            project.datasets.end(),
            [&](const Dataset &dataset) { return dataset.id == candidate; }))
      return candidate;
  }
}

ShotID nextShotId(const Project &project)
{
  return nextUnusedId("shot", project.shots);
}

ColorMapID nextColorMapId(const Project &project)
{
  return nextUnusedId("colorMap", project.colorMaps);
}

Dataset *findDataset(Project &project, const DatasetID &id)
{
  auto itr = std::find_if(project.datasets.begin(),
      project.datasets.end(),
      [&](const Dataset &d) { return d.id == id; });
  return itr == project.datasets.end() ? nullptr : &*itr;
}

const Dataset *findDataset(const Project &project, const DatasetID &id)
{
  auto itr = std::find_if(project.datasets.begin(),
      project.datasets.end(),
      [&](const Dataset &d) { return d.id == id; });
  return itr == project.datasets.end() ? nullptr : &*itr;
}

Shot *findShot(Project &project, const ShotID &id)
{
  auto itr = std::find_if(project.shots.begin(),
      project.shots.end(),
      [&](const Shot &s) { return s.id == id; });
  return itr == project.shots.end() ? nullptr : &*itr;
}

const Shot *findShot(const Project &project, const ShotID &id)
{
  auto itr = std::find_if(project.shots.begin(),
      project.shots.end(),
      [&](const Shot &s) { return s.id == id; });
  return itr == project.shots.end() ? nullptr : &*itr;
}

Shot *activeShot(Project &project)
{
  if (auto *shot = findShot(project, project.activeShotId))
    return shot;
  return project.shots.empty() ? nullptr : &project.shots.front();
}

const Shot *activeShot(const Project &project)
{
  if (auto *shot = findShot(project, project.activeShotId))
    return shot;
  return project.shots.empty() ? nullptr : &project.shots.front();
}

ColorMapRecord *findColorMap(Project &project, const ColorMapID &id)
{
  auto itr = std::find_if(project.colorMaps.begin(),
      project.colorMaps.end(),
      [&](const ColorMapRecord &c) { return c.id == id; });
  return itr == project.colorMaps.end() ? nullptr : &*itr;
}

const ColorMapRecord *findColorMap(const Project &project, const ColorMapID &id)
{
  auto itr = std::find_if(project.colorMaps.begin(),
      project.colorMaps.end(),
      [&](const ColorMapRecord &c) { return c.id == id; });
  return itr == project.colorMaps.end() ? nullptr : &*itr;
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
  return label(light_rig::findLightRig(project, id), id);
}

std::string cameraRigLabel(const Project &project, const CameraRigID &id)
{
  return label(camera_rig::findCameraRig(project, id), id);
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

} // namespace project

} // namespace vsr::scivis_studio
