// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/animation/Binding.hpp"
// vsr_core
#include "vsr/core/AnyArray.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/core/TypeMacros.hpp"
// vsr_animation
#include "vsr/animation/Interpolation.hpp"
// vsr_scene
#include "vsr/scene/AnyObjectUsePtr.hpp"
#include "vsr/scene/DefragCallback.hpp"
// std
#include <stdexcept>
#include <vector>

namespace vsr::animation {

/*
 * Binding that drives a single named parameter on a scene object over time,
 * interpolating typed keyframe values stored in an AnyArray buffer.
 *
 * Example:
 *   ObjectParameterBinding b(obj, "opacity", ANARI_FLOAT32,
 *       data, times, count, InterpolationRule::LINEAR);
 *   float v = b.data().dataAs<float>()[0];
 */
struct ObjectParameterBinding : public Binding
{
  VSR_DEFAULT_COPYABLE(ObjectParameterBinding)
  VSR_DEFAULT_MOVEABLE(ObjectParameterBinding)

  ObjectParameterBinding(scene::Scene *scene); // empty, no target
  ObjectParameterBinding(scene::Object *target,
      core::Token paramName,
      anari::DataType type,
      const void *data,
      const float *timeBase,
      size_t count,
      InterpolationRule interp = InterpolationRule::LINEAR);

  scene::Object *target() const;
  core::Token paramName() const;
  anari::DataType type() const;
  const core::AnyArray &data() const;
  const std::vector<float> &timeBase() const;
  InterpolationRule interpolation() const;

  void update(float t) override;

  template <typename T>
  void insertKeyframe(float time, const T &value);
  void removeKeyframe(size_t i);

  void onDefragment(const scene::IndexRemapper &cb) override;

  // Serialization //

  void toDataNode(core::DataNode &node) const;
  void fromDataNode(core::DataNode &node);

 private:
  void insertKeyframeImpl(float time, const void *value);

  scene::AnyObjectUsePtr<scene::Object::UseKind::ANIM> m_target;
  core::Token m_paramName;
  anari::DataType m_type{ANARI_UNKNOWN};
  core::AnyArray m_data;
  std::vector<float> m_timeBase;
  InterpolationRule m_interp{InterpolationRule::STEP};
  std::vector<scene::AnyObjectUsePtr<scene::Object::UseKind::ANIM>>
      m_objectRefs;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline void ObjectParameterBinding::insertKeyframe(float time, const T &value)
{
  if (anari::ANARITypeFor<T>::value != type()) {
    throw std::runtime_error(
        "ObjectParameterBinding::insertKeyframe<T>()"
        " called with mismatched value type");
  }

  insertKeyframeImpl(time, &value);
}

} // namespace vsr::animation
