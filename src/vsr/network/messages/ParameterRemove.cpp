// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ParameterRemove.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
namespace vsr::network::messages {

ParameterRemove::ParameterRemove(
    const vsr::scene::Object *obj, const vsr::scene::Parameter *param)
{
  if (!(obj && param)) {
    vsr::core::logError(
        "[message::ParameterRemove] No object or parameter provided");
    return;
  }

  // NOTE(jda) - node names intentionally short to reduce message size
  auto &root = m_tree.root();
  root["o"] = vsr::core::Any(obj->type(), obj->index()); // object
  root["n"] = param->name().str(); // parameter name
}

ParameterRemove::ParameterRemove(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
  vsr::core::logDebug("[message::ParameterRemove] Received message (%zu bytes)",
      msg.header.payload_length);
}

void ParameterRemove::execute()
{
  if (!m_scene) {
    vsr::core::logError(
        "[message::ParameterRemove] No scene provided for exec");
    return;
  }

  auto o = m_tree.root()["o"].getValue();
  auto obj = m_scene->getObject(o);
  if (!obj) {
    vsr::core::logError(
        "[message::ParameterRemove] Unable to find object (%s, %zu)",
        anari::toString(o.type()),
        o.getAsObjectIndex());
    return;
  }

  auto paramName = m_tree.root()["n"].getValueAs<std::string>();
  obj->removeParameter(paramName.c_str());
}

} // namespace vsr::network::messages
