// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TransferLayer.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/serialization.hpp"

namespace vsr::network::messages {

TransferLayer::TransferLayer(
    vsr::scene::Scene *scene, const vsr::scene::Layer *layer)
{
  if (!(scene && layer)) {
    vsr::core::logError(
        "[message::TransferLayer] Both scene and layer required");
    return;
  }

  // Preserved numbering: the receiver's layer must name every node by the
  // sender's index, since protocol node references carry those indices.
  auto &root = m_tree.root();
  root["n"] = scene->getLayerName(layer).str();
  vsr::io::serialize_Layer(
      *layer, root["l"], vsr::io::LayerNodeNumbering::Preserved);
}

TransferLayer::TransferLayer(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
  vsr::core::logDebug("[message::TransferLayer] Received message (%zu bytes)",
      msg.header.payload_length);
}

bool TransferLayer::execute()
{
  if (!m_scene) {
    vsr::core::logError(
        "[message::TransferLayer] No scene set to transfer data into");
    return false;
  }

  auto &root = m_tree.root();
  auto layerName = root["n"].getValueAs<std::string>();
  auto *layer = m_scene->layer(layerName);
  if (!layer) {
    layer = m_scene->addLayer(layerName.c_str());
    vsr::core::logDebug(
        "[message::TransferLayer] Creating new layer '%s'", layerName.c_str());
  } else {
    vsr::core::logDebug("[message::TransferLayer] Updating existing layer '%s'",
        layerName.c_str());
  }
  // A slot the payload records but the layer cannot honour refuses the whole
  // layer: it is left empty rather than numbered differently from the sender.
  const bool applied = vsr::io::deserialize_Layer(root["l"], *layer, *m_scene);
  if (!applied) {
    vsr::core::logError(
        "[message::TransferLayer] Layer '%s' refused: its node numbering "
        "cannot be reproduced",
        layerName.c_str());
  }
  m_scene->signalLayerStructureChanged(layer);
  return applied;
}

} // namespace vsr::network::messages
