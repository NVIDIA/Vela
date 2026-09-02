// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LightRig.h"

#include "Project.h"

#include <algorithm>
#include <string>

namespace vsr::scivis_studio::light_rig {

std::string resolveLightSubtype(std::string_view requested)
{
  return std::string(
      requested.empty() ? LIGHT_SUBTYPES.front().subtype : requested);
}

bool isKnownLightSubtype(std::string_view requested)
{
  const auto subtype = resolveLightSubtype(requested);
  return std::any_of(LIGHT_SUBTYPES.begin(),
      LIGHT_SUBTYPES.end(),
      [&](const LightSubtype &known) { return subtype == known.subtype; });
}

LightRigID nextLightRigId(const Project &project)
{
  return project::nextUnusedId("lightRig", project.lightRigs);
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
