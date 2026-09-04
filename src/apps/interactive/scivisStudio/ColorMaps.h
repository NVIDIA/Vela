// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Project.h"

#include "vsr/scene/objects/Array.hpp"

#include <string>

namespace vsr::scene {
struct Scene;
}

namespace vsr::scivis_studio::color_map {

// A color map is a ColorMapRecord paired with a scene Array of RGBA samples
// named "<colorMapId>_colormap" (the record carries no scene ref, so the name
// is the link, as with "<shotId>_camera"). These keep the pair together and
// leave the project's dirty flag to the caller. createColorMap appends a
// record named `name` as given (the caller de-duplicates) with a default-ramp
// array and returns it. removeColorMap takes the record and its array; false
// with "color map not found" for an unknown id. resolveColorMapArray finds
// the array by name. ensureColorMapArrays gives every record without one (as
// a manifest loads them) a default array.
ColorMapRecord &createColorMap(
    Project &project, vsr::scene::Scene &scene, const std::string &name);
bool removeColorMap(Project &project,
    vsr::scene::Scene &scene,
    const ColorMapID &id,
    std::string *error = nullptr);
vsr::scene::ArrayRef resolveColorMapArray(
    const vsr::scene::Scene &scene, const ColorMapID &id);
void ensureColorMapArrays(const Project &project, vsr::scene::Scene &scene);

} // namespace vsr::scivis_studio::color_map
