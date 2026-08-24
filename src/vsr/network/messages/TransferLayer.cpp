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

  auto root = m_tree.root();
  root["n"] = scene->getLayerName(layer).str();
  vsr::io::serialize_Layer(*layer, root["l"]);
}

TransferLayer::TransferLayer(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
  vsr::core::logDebug("[message::TransferLayer] Received message (%zu bytes)",
      msg.header.payload_length);
}

void TransferLayer::execute()
{
  if (!m_scene) {
    vsr::core::logError(
        "[message::TransferLayer] No scene set to transfer data into");
    return;
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
  vsr::io::deserialize_Layer(root["l"], *layer, *m_scene);
  m_scene->signalLayerStructureChanged(layer);
}

} // namespace vsr::network::messages
