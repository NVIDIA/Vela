// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Shot.h"

#include <algorithm>

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
