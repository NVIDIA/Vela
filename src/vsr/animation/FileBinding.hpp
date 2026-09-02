// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/animation/Binding.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_scene
#include "vsr/scene/LayerNodeData.hpp"
// std
#include <string>
#include <vector>

namespace vsr::animation {

struct Animation;
struct AnimationManager;

/*
 * Abstract base for animation bindings that load data from files.  Derived
 * classes live in vsr_io (which has access to format-specific importers) and
 * register runtime callbacks on the owning Animation.  The base class provides
 * the interface used by vsr_io serialization free-functions to save and
 * reconstruct these bindings without creating a cyclic dependency between
 * vsr_animation and vsr_io.
 *
 * Example:
 *   struct MyFileBinding : FileBinding {
 *     std::string kind() const override { return "myKind"; }
 *     void toDataNode(core::DataNode &n) const override { ... }
 *     void addCallbackToAnimation(Animation &a) override { ... }
 *     void update(float t) override {
 *       if (!load(t))
 *         reportLoadFailure(frameFor(t), "cannot read " + file);
 *     }
 *   };
 */
struct FileBinding : public Binding
{
  FileBinding() = default;
  FileBinding(scene::Scene *scene);
  virtual ~FileBinding() = default;

  void update(float t) override = 0;

  // Type tag written to / read from the serialized DataNode.
  virtual std::string kind() const = 0;

  // Write binding-specific data to node (called by animationToNode in vsr_io).
  virtual void toDataNode(core::DataNode &node) const = 0;

  // Layer nodes this binding writes to, if any.  Archive planning classifies
  // these exactly as it classifies a transform binding's target, so a binding
  // that drives layer state rather than an object needs no per-kind case.
  virtual std::vector<scene::LayerNodeRef> layerTargets() const;

 protected:
  // Register the runtime callback on anim.  Called both on first import and
  // after reconstruction from a legacy application-state DataNode.
  virtual void addCallbackToAnimation(Animation &anim) = 0;

  // Records that the file for `frame` (this binding's own index) could not be
  // loaded with the owning AnimationManager, which hands the record on
  // through takeLoadFailures(). Keep logging as well: the record is for
  // whoever drives time, the log for whoever reads it. A no-op for a binding
  // no Animation owns yet.
  void reportLoadFailure(int frame, std::string message) const;

 private:
  AnimationManager *m_manager{nullptr}; // set by the owning Animation

  friend struct Animation; // sets m_manager and calls addCallbackToAnimation()
                           // on new bindings
};

// Inlined definitions ////////////////////////////////////////////////////////

inline FileBinding::FileBinding(scene::Scene *scene) : Binding(scene) {}

inline std::vector<scene::LayerNodeRef> FileBinding::layerTargets() const
{
  return {};
}

} // namespace vsr::animation
