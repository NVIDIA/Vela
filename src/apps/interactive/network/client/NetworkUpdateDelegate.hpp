// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/UpdateDelegate.hpp"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"

namespace vsr::network {

struct NetworkUpdateDelegate : public vsr::scene::BaseUpdateDelegate
{
  NetworkUpdateDelegate(
      vsr::scene::Scene *scene, vsr::network::NetworkChannel *channel);
  ~NetworkUpdateDelegate() override = default;

  void setEnabled(bool enabled);
  void setNetworkChannel(vsr::network::NetworkChannel *channel);

  // Update signals //

  void signalObjectAdded(const vsr::scene::Object *) override;
  void signalParameterUpdated(
      const vsr::scene::Object *, const vsr::scene::Parameter *) override;
  void signalParameterRemoved(
      const vsr::scene::Object *, const vsr::scene::Parameter *) override;
  void signalParameterBatchUpdated(const vsr::scene::Object *,
      const std::vector<const vsr::scene::Parameter *> &) override;
  void signalArrayMapped(const vsr::scene::Array *) override;
  void signalArrayUnmapped(const vsr::scene::Array *) override;
  void signalObjectParameterUseCountZero(const vsr::scene::Object *obj) override;
  void signalObjectLayerUseCountZero(const vsr::scene::Object *obj) override;
  void signalObjectRemoved(const vsr::scene::Object *) override;
  void signalRemoveAllObjects() override;
  void signalLayerAdded(const vsr::scene::Layer *) override;
  void signalLayerStructureUpdated(const vsr::scene::Layer *) override;
  void signalLayerTransformUpdated(const vsr::scene::Layer *) override;
  void signalLayerRemoved(const vsr::scene::Layer *) override;
  void signalActiveLayersChanged() override;
  void signalObjectFilteringChanged() override;
  void signalInvalidateCachedObjects() override;

 private:
  bool isReady(const char *fcn) const;

  vsr::scene::Scene *m_scene{nullptr};
  vsr::network::NetworkChannel *m_channel{nullptr};
  bool m_enabled{true};
};

} // namespace vsr::network
