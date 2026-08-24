// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Manipulator.hpp"
// anari
#include <anari/anari_cpp.hpp>

namespace vsr::rendering {

void updateCameraParametersPerspective(
    anari::Device d, anari::Camera c, const Manipulator &m);

void updateCameraParametersOrthographic(
    anari::Device d, anari::Camera c, const Manipulator &m);

} // namespace vsr::rendering
