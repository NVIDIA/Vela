// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SceneEditMessages.h"
#include "PayloadCommon.h"

namespace vsr::scivis_studio::protocol {

void toNode(const SetObjectParameter &p, vsr::core::DataNode &n)
{
  writeChildNode(n, "object", p.object);
  writeChild(n, "name", p.name);
  n["value"].setValue(p.value);
}

bool fromNode(const vsr::core::DataNode &n, SetObjectParameter &p)
{
  if (!readChildNode(n, "object", p.object) || !readChild(n, "name", p.name))
    return false;
  const auto *value = n.child("value");
  if (!value || value->holdsArray() || !value->getValue().valid())
    return false;
  p.value = value->getValue();
  return true;
}

} // namespace vsr::scivis_studio::protocol
