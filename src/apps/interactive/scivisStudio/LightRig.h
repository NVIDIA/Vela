// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Dataset.h"

#include <array>
#include <string>
#include <string_view>

namespace vsr::scivis_studio {

struct Project;

struct LightRig
{
  LightRigID id;
  std::string name;
  SceneNodeRef rootNode;

  // Runtime-only name of the asset path owned by this rig.
  std::string persistedName;
};

namespace light_rig {

// An ANARI light subtype a rig may hold, with the label the editors show.
struct LightSubtype
{
  const char *label;
  const char *subtype;
};

// The subtypes the Light Rig editors offer and the server admits, the
// default first: an empty subtype means LIGHT_SUBTYPES.front() wherever a
// light is added.
constexpr std::array<LightSubtype, 5> LIGHT_SUBTYPES = {{
    {"Directional", "directional"},
    {"Point", "point"},
    {"Quad", "quad"},
    {"Spot", "spot"},
    {"Ring", "ring"},
}};

// The subtype `requested` names: itself, or the default when empty.
std::string resolveLightSubtype(std::string_view requested);
// True when `requested` (empty included) resolves to one of LIGHT_SUBTYPES.
bool isKnownLightSubtype(std::string_view requested);

// Collection lookups within a Project. (Light rig value data lives in the scene
// graph as a node subtree, so its file IO is a scene-aware ProjectContext
// method rather than a free function here.)
LightRigID nextLightRigId(const Project &project);
LightRig *findLightRig(Project &project, const LightRigID &id);
const LightRig *findLightRig(const Project &project, const LightRigID &id);

} // namespace light_rig

} // namespace vsr::scivis_studio
