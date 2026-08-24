// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "NewObject.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/serialization.hpp"

namespace vsr::network::messages {

NewObject::NewObject(const vsr::scene::Object *o)
{
  if (!o) {
    vsr::core::logError("[message::NewObject] No client object provided");
    return;
  }

  vsr::io::serialize_Object(*o, m_tree.root(), true);
}

NewObject::NewObject(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
  vsr::core::logDebug("[message::NewObject] Received message (%zu bytes)",
      msg.header.payload_length);
}

void NewObject::execute()
{
  if (!m_scene) {
    vsr::core::logError("[message::NewObject] No scene provided for exec");
    return;
  }

  vsr::io::deserialize_Object(*m_scene, m_tree.root());
}

} // namespace vsr::network::messages
