// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/animation/Binding.hpp"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <functional>

namespace vsr::animation {

/*
 * Binding that invokes a user-supplied callback whenever the animation time
 * is updated. The callback receives only the current time; any scene objects
 * to modify should be captured directly in the closure.
 *
 * Example:
 *   auto *obj = scene.getObject(...);
 *   animation.addCallbackBinding([obj](float t) {
 *     float v = std::sin(t * 2.f * M_PI);
 *     obj->setParameter("opacity", ANARI_FLOAT32, &v);
 *   });
 */
struct CallbackBinding : public Binding
{
  VSR_DEFAULT_COPYABLE(CallbackBinding)
  VSR_DEFAULT_MOVEABLE(CallbackBinding)

  using Callback = std::function<void(float)>;

  CallbackBinding(Callback callback);

  void update(float t) override;

 private:
  Callback m_callback;
};

} // namespace vsr::animation
