// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_network
#include "vsr/network/Message.hpp"
// vsr_scene
#include "vsr/scene/UpdateDelegate.hpp"
// std
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

using MessageSink = std::function<void(vsr::network::Message &&)>;

/*
 * Update delegate installed on the Structural Mirror that turns local edits
 * into the optimistic client->server messages of SceneEditMessages.h:
 * parameter set/batch -> one SetObjectParameter per parameter (array-typed
 * values are skipped: arrays never ride that message), parameter removed ->
 * RemoveObjectParameter, metadata set/removed/batch -> one SetObjectMetadata
 * naming every key that changed, array-valued keys included (v12), so a
 * volume's opacityControlPoints travels with the rest.
 *
 * Array *contents* travel too, but only for arrays the UI has declared
 * editable with setArrayEditable(). The wire would carry any array; the
 * client sends only the ones a panel has hydrated and opened an editor on,
 * so an incidental map/unmap of a dataset's field data never becomes a
 * multi-gigabyte upload. Inside an update batch the dirty arrays are
 * collected and flushed once at the end, so dragging one knob sends one
 * message however many times the widget rewrites the samples.
 *
 * Everything else the mirror can signal (object add/remove, layer structure)
 * is not in the Studio message set and is ignored.
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
  void signalMetadataUpdated(
      const vsr::scene::Object *o, const char *name) override;
  void signalMetadataBatchUpdated(const vsr::scene::Object *o,
      const std::vector<std::string> &names) override;
  void signalArrayUnmapped(const vsr::scene::Array *a) override;
  void signalUpdateBatchBegin() override;
  void signalUpdateBatchEnd() override;

  // Whether local writes to this array (by its mirror index) are sent to the
  // server. Off for every array until a panel hydrates one; a panel clears it
  // when it drops the array, and a mirror replacement clears all of them.
  void setArrayEditable(size_t arrayIndex, bool editable);
  void clearEditableArrays();
  bool arrayEditable(size_t arrayIndex) const;

 private:
  void sendParameter(
      const vsr::scene::Object *o, const vsr::scene::Parameter *p);
  // One message for `names`, skipping array-valued keys; sends nothing when
  // that leaves no entry.
  void sendMetadata(
      const vsr::scene::Object *o, const std::vector<std::string> &names);
  void sendArray(const vsr::scene::Array *a);

  MessageSink m_send;
  bool m_enabled{false};
  int m_batchDepth{0};
  std::set<size_t> m_editableArrays;
  // Arrays unmapped inside the open batch, by mirror index. Held as pointers
  // because a batch never outlives the frame that opened it.
  std::map<size_t, const vsr::scene::Array *> m_pendingArrays;
};

} // namespace vsr::scivis_studio::client
