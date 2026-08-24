// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TransferScene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"

namespace vsr::network::messages {

TransferScene::TransferScene(vsr::scene::Scene *scene, bool includeArrayData)
{
  if (!scene) {
    vsr::core::logError(
        "[message::TransferScene] No scene set to transfer data from");
    return;
  }

  if (!vsr::io::serialize_SceneArchive(*scene,
          m_tree.root(),
          includeArrayData ? vsr::io::ArrayDataPolicy::IncludeData
                           : vsr::io::ArrayDataPolicy::ProxyOnly)) {
    vsr::core::logError(
        "[message::TransferScene] Failed to serialize Scene Archive");
  }
}

TransferScene::TransferScene(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
  vsr::core::logStatus("[message::TransferScene] Received message (%zu bytes)",
      msg.header.payload_length);
}

void TransferScene::execute()
{
  if (!m_scene) {
    vsr::core::logError(
        "[message::TransferScene] No scene set to transfer data into");
    return;
  }

  if (!vsr::io::deserialize_SceneArchive(*m_scene, m_tree.root())) {
    vsr::core::logError(
        "[message::TransferScene] Failed to deserialize Scene Archive");
  }
}

} // namespace vsr::network::messages
