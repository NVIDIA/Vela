// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ParameterChange.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/serialization.hpp"

namespace vsr::network::messages {

ParameterChange::ParameterChange(
    const vsr::scene::Object *obj, const vsr::scene::Parameter *param)
    : ParameterChange(obj, &param, 1)
{}

ParameterChange::ParameterChange(const vsr::scene::Object *obj,
    const vsr::scene::Parameter *const *params,
    size_t np)
{
  if (!obj) {
    vsr::core::logError(
        "[message::ParameterChange] No object provided for multi-param ctor");
    return;
  }

  auto &root = m_tree.root();
  root["o"] = vsr::core::Any(obj->type(), obj->index()); // object

  auto &ps = root["p"];

  for (size_t i = 0; i < np; ++i) {
    auto &paramNode = ps.append(); // parameter node
    auto *param = params[i];
    paramNode["n"] = param->name().str(); // parameter name
    vsr::io::serialize_Parameter(
        *param, paramNode["v"]); // parameter value + info
  }
}

ParameterChange::ParameterChange(const Message &msg, vsr::scene::Scene *scene)
    : StructuredMessage(msg), m_scene(scene)
{
#if 0
  vsr::core::logDebug(
      "[message::ParameterChange] Received message (%zu bytes)",
      msg.header.payload_length);
#endif
}

void ParameterChange::execute()
{
  if (!m_scene) {
    vsr::core::logError(
        "[message::ParameterChange] No scene provided for exec");
    return;
  }

  auto &root = m_tree.root();
  auto o = root["o"].getValue();
  auto obj = m_scene->getObject(o);
  if (!obj) {
    vsr::core::logError(
        "[message::ParameterChange] Unable to find object (%s, %zu)",
        anari::toString(o.type()),
        o.getAsObjectIndex());
    return;
  }

  auto &pn = root["p"];

  pn.foreach_child([&](core::DataNode &child) {
    auto paramName = child["n"].getValueAs<std::string>();
    auto &p = obj->addParameter(paramName.c_str()); // parameter may be new
    vsr::io::deserialize_Parameter(child["v"], p);
  });
}

} // namespace vsr::network::messages
