// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Project.h"
#include "ProjectAssetTransaction.h"

#include "vsr/core/DataTree.hpp"
#include "vsr/scene/Layer.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vsr::animation {
struct AnimationManager;
}

namespace vsr::core {
struct DataNode;
}

namespace vsr::scene {
struct Scene;
}

namespace vsr::scivis_studio {

namespace detail {
struct ProjectOpenState;
}

struct ProjectSaveRequest
{
  ProjectSaveRequest(const Project &project,
      const vsr::scene::Scene &scene,
      vsr::animation::AnimationManager &animationManager,
      std::filesystem::path directory);

  const Project &project;
  const vsr::scene::Scene &scene;
  vsr::animation::AnimationManager &animationManager;
  std::filesystem::path directory;
  std::vector<std::filesystem::path> pendingAssetRemovals;
  // {windows, layout, settings}; see ProjectContext::saveProject.
  const vsr::core::DataNode *uiState{nullptr};
};

struct ProjectSaveResult
{
  Project project;
  ProjectSavePlan plan;
};

bool buildProjectSavePlan(const ProjectSaveRequest &request,
    ProjectSaveResult &result,
    std::string *error = nullptr);

// Overrides applied while staging a project open. openUnloaded changes each
// dataset's initial residency; the project opens dirty when that override
// diverges from the manifest, so a subsequent save persists actual residency.
// bookkeeping instead opens without building any dataset runtime
// representation while leaving recorded residency untouched and the project
// clean: residency records intent, not process state, so a bookkeeping open
// must round-trip it unchanged. bookkeeping wins when both are set.
struct ProjectOpenOptions
{
  bool openUnloaded{false};
  bool bookkeeping{false};
};

struct ProjectOpenStage
{
  Project project;
  vsr::core::DataTree ui;

 private:
  std::shared_ptr<detail::ProjectOpenState> m_state;

  friend bool stageProjectOpen(const std::filesystem::path &,
      ProjectOpenStage &,
      const ProjectOpenOptions &,
      std::string *);
  friend bool applyProjectOpen(ProjectOpenStage &,
      vsr::scene::Scene &,
      vsr::animation::AnimationManager &,
      std::string *);
};

bool stageProjectOpen(const std::filesystem::path &directory,
    ProjectOpenStage &stage,
    const ProjectOpenOptions &options = {},
    std::string *error = nullptr);
bool applyProjectOpen(ProjectOpenStage &stage,
    vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager,
    std::string *error = nullptr);

// The child of `parent` named `name`, or null (also for a null parent): the
// lookup behind the studio layer's "<collection>/<id>" node convention.
vsr::scene::LayerNodeRef findDirectChild(
    vsr::scene::LayerNodeRef parent, const std::string &name);

} // namespace vsr::scivis_studio
