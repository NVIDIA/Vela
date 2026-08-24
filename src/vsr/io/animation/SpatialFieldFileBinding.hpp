// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/animation/Animation.hpp"
#include "vsr/animation/FileBinding.hpp"
#include "vsr/scene/ObjectUsePtr.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/SpatialField.hpp"
#include "vsr/scene/objects/Volume.hpp"
// std
#include <string>
#include <vector>

namespace vsr::io {

/*
 * Binding that drives a Volume's spatial field by loading a different file at
 * each animation time step.  Time t=0.0 selects files[0]; t=1.0 selects
 * files[N-1].  Only the currently-visible frame's SpatialField is held in
 * memory; the previous frame's field is removed from the scene on each step.
 *
 * Example:
 *   auto field0 = import_spatial_field(scene, files[0].c_str());
 *   auto vol = ...; // volume with field0 as "value"
 *   auto &b = anim.emplaceFileBinding<SpatialFieldFileBinding>(&scene,
 * vol.data(), field0, files);
 *
 *   // Or use the higher-level helper:
 *   import_volume_animation(scene, animMgr, files, location);
 */
struct SpatialFieldFileBinding : public vsr::animation::FileBinding
{
  // Construct the binding.  `initialField` must already be set as the "value"
  // parameter on `volume` and corresponds to files[0].
  SpatialFieldFileBinding(scene::Scene *scene,
      scene::Volume *volume,
      scene::SpatialFieldRef initialField,
      std::vector<std::string> files);

  // FileBinding interface //

  std::string kind() const override;

  // Writes targetIndex (the volume's object-pool index) and the file list.
  void toDataNode(vsr::core::DataNode &node) const override;

  void onDefragment(const scene::IndexRemapper &cb) override;

  // Load the appropriate file for time t.  No-ops if the frame has not changed.
  void update(float t) override;

  size_t frameCount() const;
  int currentFrame() const;

 private:
  // Registers a CallbackBinding on `anim` that loads the appropriate file on
  // each time change.  Does NOT add this binding to anim.fileBindings().
  // Called both during initial import and during reconstruction from a
  // DataNode.
  void addCallbackToAnimation(vsr::animation::Animation &anim) override;

  scene::ObjectUsePtr<scene::Volume, scene::Object::UseKind::ANIM> m_volume;
  scene::ObjectUsePtr<scene::SpatialField, scene::Object::UseKind::ANIM>
      m_currentField;
  std::vector<std::string> m_files;
  int m_currentFrame{0};
};

} // namespace vsr::io
