// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/DefragCallback.hpp"

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::animation {

/*
 * Base class for animation bindings.  Each binding owns a reference to its
 * scene and implements update(float t) to evaluate and apply its animation
 * state at time t.  Derived types add target-specific data and interpolation
 * logic in their override of update().
 *
 * Example:
 *   struct MyBinding : Binding {
 *     MyBinding(scene::Scene *s) : Binding(s) {}
 *     void update(float t) override { ... }
 *   };
 */
struct Binding
{
  Binding() = default;
  Binding(scene::Scene *scene);
  virtual ~Binding() = default;

  virtual void update(float t) = 0;
  virtual void onDefragment(const scene::IndexRemapper &cb) {}

  scene::Scene *scene() const;

 private:
  scene::Scene *m_scene{nullptr};
};

} // namespace vsr::animation
