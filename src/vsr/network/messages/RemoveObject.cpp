// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RemoveObject.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
namespace vsr::network::messages {

RemoveObject::RemoveObject(const vsr::scene::Object *o)
{
  if (!o) {
    vsr::core::logError("[message::RemoveObject] No client object provided");
    return;
  }

  m_tree.root() = vsr::core::Any(o->type(), o->index());
}

RemoveObject::RemoveObject(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
  vsr::core::logDebug("[message::RemoveObject] Received message (%zu bytes)",
      msg.header.payload_length);
}

void RemoveObject::execute()
{
  if (!m_scene) {
    vsr::core::logError("[message::RemoveObject] No scene provided for exec");
    return;
  }

  m_scene->removeObject(m_tree.root().getValue());
}

} // namespace vsr::network::messages
