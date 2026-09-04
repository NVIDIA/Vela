// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Project.h"

#include <string>

namespace vsr::scene {
struct Object;
struct Scene;
} // namespace vsr::scene

namespace vsr::scivis_studio::shot {

// The record side of ProjectContext's removeShot and updateShot: validation,
// the stored Shot and the shot's scene objects. Neither marks the project
// dirty nor touches the animation manager; the caller re-syncs it and
// re-applies the active shot when that shot changed.

// Refuses an unknown id ("shot not found") and the last shot ("cannot remove
// the last shot"). Otherwise removes the shot's camera and its
// studio/shots/<id> node from `scene` (when one is given), erases the record
// and, when the active shot went, makes the first remaining one active;
// `activeChanged` reports that.
bool removeShot(Project &project,
    vsr::scene::Scene *scene,
    const ShotID &id,
    bool &activeChanged,
    std::string *error = nullptr);

// Validates `incoming` against the project (its rig ids must exist) and, when
// a scene is given, its renderer against its library; then replaces the
// stored Shot with a copy that keeps the runtime camera ref and `playing`,
// keeps currentFrame while the shot plays, clamps the ranges
// (clampToValidRanges) and drops bindings to unknown datasets. False with
// `error` leaves the record untouched.
bool updateShot(Project &project,
    const vsr::scene::Scene *scene,
    const Shot &incoming,
    std::string *error = nullptr);

// The shot's camera object: the one its id names ("<id>_camera"), whose ref
// is written back to `shot.camera`, else whatever `shot.camera` already
// refers to. Null when neither exists.
vsr::scene::Object *resolveShotCamera(
    const vsr::scene::Scene &scene, Shot &shot);

} // namespace vsr::scivis_studio::shot
