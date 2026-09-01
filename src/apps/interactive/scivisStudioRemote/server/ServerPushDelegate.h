// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_network
#include "vsr/network/Message.hpp"
// vsr_scene
#include "vsr/scene/UpdateDelegate.hpp"
// std
#include <functional>

namespace vsr::scene {
struct Scene;
}

namespace vsr::scivis_studio::server {

/*
 * The server's scene push delegate: turns structural Scene mutations into the
 * Studio scene messages that keep the client's Structural Mirror current.
 * Identity, lifecycle and structure are server-authoritative, so those are
 * pushed (ObjectAdded, ObjectRemoved, whole-layer TransferLayer snapshots);
 * parameter values are optimistic and one-way and are deliberately NOT
 * pushed.
 *
 * signalRemoveAllObjects does not send anything itself: it fires at the start
 * of a rebuild, when the scene is momentarily empty, so a TransferScene
 * serialized here would describe nothing. The delegate instead asks the owner
 * to resend the scene, and the render loop sends one TransferScene after the
 * mutation that emptied it has finished -- the same snapshot a bootstrap
 * sends, which is the simplest message that leaves the mirror correct.
 *
 * Origin-based echo suppression: the owner disables the delegate while it
 * applies a client's edits and during bootstrap (the TransferScene there
 * already covers everything). Signals arrive synchronously on whichever
 * thread mutates the Scene; in this server only the render loop does, and
 * NetworkChannel::send() is thread-safe regardless.
 *
 * Example:
 *   auto *push = scene.updateDelegate().emplace<ServerPushDelegate>(&scene,
 *       [&](vsr::network::Message &&m) { channel.send(std::move(m)); },
 *       [&] { sceneResendPending = true; });
 *   push->setEnabled(false);
 *   applyClientEdit();
 *   push->setEnabled(true);
 */
struct ServerPushDelegate : public vsr::scene::EmptyUpdateDelegate
{
  using SendFunction = std::function<void(vsr::network::Message &&)>;
  using ResendSceneFunction = std::function<void()>;

  ServerPushDelegate(vsr::scene::Scene *scene,
      SendFunction send,
      ResendSceneFunction requestSceneResend);
  ~ServerPushDelegate() override = default;

  bool enabled() const;
  void setEnabled(bool enabled);

  void signalObjectAdded(const vsr::scene::Object *obj) override;
  void signalObjectRemoved(const vsr::scene::Object *obj) override;
  void signalRemoveAllObjects() override;
  void signalLayerAdded(const vsr::scene::Layer *layer) override;
  void signalLayerStructureUpdated(const vsr::scene::Layer *layer) override;
  void signalLayerTransformUpdated(const vsr::scene::Layer *layer) override;

 private:
  void sendLayer(const vsr::scene::Layer *layer);

  vsr::scene::Scene *m_scene{nullptr};
  SendFunction m_send;
  ResendSceneFunction m_requestSceneResend;
  bool m_enabled{false};
};

} // namespace vsr::scivis_studio::server
