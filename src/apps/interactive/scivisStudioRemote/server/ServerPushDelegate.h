// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scene
#include "vsr/scene/UpdateDelegate.hpp"

namespace vsr::scivis_studio::server {

/*
 * The server's scene recorder: it sends nothing. Every structural Scene
 * mutation -- an object added or removed, a layer added, restructured or
 * moved, the whole scene emptied -- only marks the scene dirty. The owner
 * asks at the commit point (the reply or task ending that a Project
 * Snapshot rides on) and sends one TransferScene there.
 *
 * That timing is the point. Scene signals fire *during* a mutation:
 * signalObjectAdded fires from Scene::createObject, before the importer has
 * set a single parameter, so an object serialized there is an empty shell,
 * and signalRemoveAllObjects fires when the scene is momentarily empty.
 * Deferring to the commit point is what makes the mirror's objects carry
 * their parameter values, and it collapses the O(nodes) whole-layer
 * serializations an import used to cause into one snapshot. The cost is
 * that a mutation is all-or-nothing on the client: no incremental progress,
 * and the replaced mirror clears the selection.
 *
 * Origin-based echo suppression: the owner disables the recorder while it
 * applies a client's edits and during bootstrap (whose own TransferScene
 * already covers everything), so a client's parameter edit does not come
 * back to it as a snapshot. Signals arrive synchronously on whichever
 * thread mutates the Scene; in this server only the render loop does.
 *
 * Example:
 *   auto *recorder = scene.updateDelegate().emplace<ServerPushDelegate>();
 *   recorder->setEnabled(true);
 *   importDataset();
 *   if (recorder->sceneDirty()) {
 *     recorder->clearSceneDirty();
 *     sendTransferScene();
 *   }
 */
struct ServerPushDelegate : public vsr::scene::EmptyUpdateDelegate
{
  ServerPushDelegate() = default;
  ~ServerPushDelegate() override = default;

  bool enabled() const;
  void setEnabled(bool enabled);

  // Set by any recorded signal, cleared by the owner once it has sent the
  // snapshot that covers them.
  bool sceneDirty() const;
  void clearSceneDirty();

  void signalObjectAdded(const vsr::scene::Object *obj) override;
  void signalObjectRemoved(const vsr::scene::Object *obj) override;
  void signalRemoveAllObjects() override;
  void signalLayerAdded(const vsr::scene::Layer *layer) override;
  void signalLayerStructureUpdated(const vsr::scene::Layer *layer) override;
  void signalLayerTransformUpdated(const vsr::scene::Layer *layer) override;

 private:
  void markDirty();

  bool m_enabled{false};
  bool m_sceneDirty{false};
};

} // namespace vsr::scivis_studio::server
