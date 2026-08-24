// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/animation/Binding.hpp"

namespace vsr::animation {

Binding::Binding(scene::Scene *scene) : m_scene(scene) {}

scene::Scene *Binding::scene() const
{
  return m_scene;
}

} // namespace vsr::animation
