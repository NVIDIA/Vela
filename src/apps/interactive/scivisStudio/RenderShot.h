// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"

#include <filesystem>
#include <functional>
#include <string>

namespace vsr::scivis_studio {

struct RenderShotProgress
{
  std::function<bool(int frame, int totalFrames)> onFrame;
};

// What renderActiveShotToFrames loaded to materialize the shot, so the prior
// residency (and what a save would persist) can be restored afterward.
struct ShotDatasetResidencyRestore
{
  std::vector<DatasetID> loadedForRender;
  bool projectWasDirty{false};
};

// Bring every bound, enabled dataset of the shot fully resident regardless of
// its stored residency. Fails up front — restoring anything it already
// loaded — when a dataset cannot be made resident.
bool makeShotDatasetsResident(ProjectContext &projectContext,
    const Shot &shot,
    ShotDatasetResidencyRestore &restore,
    std::string *error = nullptr);

// Unload the datasets that were loaded only for rendering and restore the
// project dirty flag captured when materialization began.
void restoreShotDatasetResidency(
    ProjectContext &projectContext, const ShotDatasetResidencyRestore &restore);

// How a shot render ended. `completed` when every frame was written;
// `cancelled` when onFrame stopped it; otherwise `error` says why it never
// started (an unsaved project, a missing camera, a dataset that could not be
// made resident) or stopped early. framesCompleted counts the frames written
// before it ended -- they stay on disk under outputDirectory.
struct RenderShotResult
{
  bool completed{false};
  bool cancelled{false};
  std::string error;
  int framesCompleted{0};
  std::filesystem::path outputDirectory;
};

// Renders the active shot's frames to <project>/renders/<shotId>/. The
// result tells a completed render from a cancel or a failure and how far it
// got; a throw from a frame's load or encode propagates.
RenderShotResult renderActiveShotToFrames(
    ProjectContext &projectContext, RenderShotProgress *progress = nullptr);

} // namespace vsr::scivis_studio
