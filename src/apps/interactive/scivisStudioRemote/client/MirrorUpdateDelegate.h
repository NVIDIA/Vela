// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_network
#include "vsr/network/Message.hpp"
// vsr_scene
#include "vsr/scene/UpdateDelegate.hpp"
// std
#include <functional>

namespace vsr::scivis_studio::client {

using MessageSink = std::function<void(vsr::network::Message &&)>;

/*
 * Update delegate installed on the Structural Mirror that turns local edits
 * into the optimistic client->server messages of SceneEditMessages.h:
 * parameter set/batch -> one SetObjectParameter per parameter (array-typed
 * values are skipped: arrays never ride that message), parameter removed ->
 * RemoveObjectParameter. Everything else the mirror can signal (object
 * add/remove, layer structure, arrays) is not in the Studio message set and
 * is ignored.
 *
 * Layer transforms are also ignored: signalLayerTransformUpdated() names only
 * the Layer, not the node that moved, so SetNodeTransform cannot be built
 * from it. Node edits belong to the UI that knows which node it moved.
 *
 * Disabled, it emits nothing; ServerConnection disables it while applying
 * server pushes and bootstrap so remote edits never echo back (origin-based
 * echo suppression), and whenever it is not Connected.
 *
 * Example:
 *   auto *d = mirror.updateDelegate().emplace<MirrorUpdateDelegate>(
 *       [&](vsr::network::Message &&m) { channel.send(std::move(m)); });
 *   d->setEnabled(false);   // while applying a TransferScene
 */
struct MirrorUpdateDelegate : public vsr::scene::EmptyUpdateDelegate
{
  MirrorUpdateDelegate(MessageSink send);
  ~MirrorUpdateDelegate() override = default;

  bool enabled() const;
  void setEnabled(bool enabled);

  void signalParameterUpdated(
      const vsr::scene::Object *o, const vsr::scene::Parameter *p) override;
  void signalParameterBatchUpdated(const vsr::scene::Object *o,
      const std::vector<const vsr::scene::Parameter *> &ps) override;
  void signalParameterRemoved(
      const vsr::scene::Object *o, const vsr::scene::Parameter *p) override;

 private:
  void sendParameter(
      const vsr::scene::Object *o, const vsr::scene::Parameter *p);

  MessageSink m_send;
  bool m_enabled{false};
};

} // namespace vsr::scivis_studio::client
