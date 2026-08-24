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

} // namespace vsr::rendering
