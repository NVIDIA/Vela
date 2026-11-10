// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <vector>

namespace vsr::ui::imgui {

/*
 * The renderers of one device in a scene, in the scene's storage order (the
 * order of Scene::renderersOfDevice()), and the one in use. Once bound, the
 * list follows the scene through an update delegate: the device's renderers
 * join it when the scene adds them and leave it before the scene erases them,
 * so its use-counted refs never outlive their objects.
 *
 * A non-empty list always has a current renderer. When the current one is
 * removed, Scene::rendererOfSubtypeOrFirst() picks its replacement: the
 * renderer of the same subtype, otherwise the device's first renderer.
 *
 * The bound scene must outlive the binding: unbind() (or destroy the list)
 * before the scene goes away.
 */
struct RendererList
{
  RendererList() = default;
  ~RendererList();

  VSR_NOT_COPYABLE(RendererList)
  VSR_NOT_MOVEABLE(RendererList)

  bool empty() const;
  const std::vector<vsr::scene::RendererAppRef> &objects() const;
  vsr::scene::RendererAppRef current() const;

  // List the device's renderers and follow the scene from now on; the first
  // one becomes current.
  void bind(vsr::scene::Scene &scene, vsr::core::Token deviceName);
  // Empty the list and stop following the scene.
  void unbind();
  // Make the listed renderer with this object index current. Returns false,
  // changing nothing, when no listed renderer has it.
  bool select(size_t objectIndex);

 private:
  struct SceneDelegate;
  friend struct SceneDelegate;

  void rendererAdded(const vsr::scene::Renderer &r);
  void rendererRemoved(const vsr::scene::Renderer &r);
  void allObjectsRemoved();
  std::vector<vsr::scene::RendererAppRef>::iterator find(size_t objectIndex);

  vsr::scene::Scene *m_scene{nullptr};
  vsr::core::Token m_deviceName;
  vsr::scene::BaseUpdateDelegate *m_delegate{nullptr};
  std::vector<vsr::scene::RendererAppRef> m_objects;
  vsr::scene::RendererAppRef m_current;
};

} // namespace vsr::ui::imgui
