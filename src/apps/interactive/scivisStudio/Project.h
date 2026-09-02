// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CameraRig.h"
#include "Dataset.h"
#include "LightRig.h"
#include "Shot.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace vsr::scivis_studio {

struct ColorMapRecord
{
  ColorMapID id;
  std::string name;
};

struct Project
{
  std::string name{"Untitled"};
  std::filesystem::path projectDirectory;
  std::vector<Dataset> datasets;
  std::vector<Shot> shots;
  std::vector<LightRig> lightRigs;
  std::vector<CameraRig> cameraRigs;
  std::vector<ColorMapRecord> colorMaps;
  ShotID activeShotId;
  uint64_t nextDatasetOrdinal{1};
  bool dirty{false};

  bool isSaved() const;
  void markDirty();
  void markClean();
};

namespace project {

std::string makeGeneratedId(const char *prefix, size_t ordinal);
// The lowest "<prefix>_NNNN" from items.size()+1 upward that no item in
// `items` uses as its id; removals leave gaps the next id must not reuse
// while a later entry still holds it.
template <typename ItemT>
std::string nextUnusedId(const char *prefix, const std::vector<ItemT> &items);
DatasetID nextDatasetId(Project &project);
ShotID nextShotId(const Project &project);
ColorMapID nextColorMapId(const Project &project);

Dataset *findDataset(Project &project, const DatasetID &id);
const Dataset *findDataset(const Project &project, const DatasetID &id);
Shot *findShot(Project &project, const ShotID &id);
const Shot *findShot(const Project &project, const ShotID &id);
Shot *activeShot(Project &project);
const Shot *activeShot(const Project &project);
ColorMapRecord *findColorMap(Project &project, const ColorMapID &id);
const ColorMapRecord *findColorMap(
    const Project &project, const ColorMapID &id);

} // namespace project

// Inlined definitions ////////////////////////////////////////////////////////

template <typename ItemT>
inline std::string project::nextUnusedId(
    const char *prefix, const std::vector<ItemT> &items)
{
  // Counting from size()+1 finds a free id in one step until an item was
  // removed from the middle; after that the loop skips the survivors.
  for (size_t ordinal = items.size() + 1;; ++ordinal) {
    const auto candidate = makeGeneratedId(prefix, ordinal);
    if (std::none_of(items.begin(), items.end(), [&](const ItemT &item) {
          return item.id == candidate;
        }))
      return candidate;
  }
}

} // namespace vsr::scivis_studio
