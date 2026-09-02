// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Manipulator.hpp"
// vsr_core
#include "vsr/scene/objects/Camera.hpp"

namespace vsr::rendering {

void updateCameraObject(vsr::scene::Camera &c,
    const Manipulator &m,
    bool includeManipulatorMetadata = true);

void updateManipulatorFromCamera(Manipulator &m, const vsr::scene::Camera &c);

// The pose route: adopts the camera's position/direction/up parameters
// (Manipulator::setPose) rather than its manipulator metadata, for a camera
// edited by something that writes parameters only, like a remote client's
// SetObjectParameter. Leaves the manipulator alone when the camera lacks any
// of the three.
void updateManipulatorFromCameraPose(
    Manipulator &m, const vsr::scene::Camera &c);

} // namespace vsr::rendering
