// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LightRig.h"

#include "Project.h"

#include <algorithm>

namespace vsr::scivis_studio::light_rig {

LightRigID nextLightRigId(const Project &project)
{
  return project::makeGeneratedId("lightRig", project.lightRigs.size() + 1);
}

LightRig *findLightRig(Project &project, const LightRigID &id)
{
  auto itr = std::find_if(project.lightRigs.begin(),
      project.lightRigs.end(),
      [&](const LightRig &r) { return r.id == id; });
  return itr == project.lightRigs.end() ? nullptr : &*itr;
}

const LightRig *findLightRig(const Project &project, const LightRigID &id)
{
  auto itr = std::find_if(project.lightRigs.begin(),
      project.lightRigs.end(),
      [&](const LightRig &r) { return r.id == id; });
  return itr == project.lightRigs.end() ? nullptr : &*itr;
}

} // namespace vsr::scivis_studio::light_rig
