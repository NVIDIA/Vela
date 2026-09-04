// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataNodeFields.h"

namespace vsr::scivis_studio {

// Scene identity /////////////////////////////////////////////////////////////

void toNode(const SceneObjectRef &ref, vsr::core::DataNode &node)
{
  if (anari::isObject(ref.type))
    node.setValueObject(ref.type, ref.objectIndex);
}

bool fromNode(const vsr::core::DataNode &node, SceneObjectRef &ref)
{
  SceneObjectRef out;
  if (node.holdsObjectIdx())
    node.getValueAsObjectIdx(&out.type, &out.objectIndex);
  else if (!node.empty() || node.numChildren() != 0)
    return false;
  ref = out;
  return true;
}

void toNode(const SceneNodeRef &ref, vsr::core::DataNode &node)
{
  writeChild(node, "layerName", ref.layerName);
  writeChild(node, "nodeIndex", ref.nodeIndex);
}

bool fromNode(const vsr::core::DataNode &node, SceneNodeRef &ref)
{
  SceneNodeRef out;
  if (!readChild(node, "layerName", out.layerName)
      || !readChild(node, "nodeIndex", out.nodeIndex))
    return false;
  ref = std::move(out);
  return true;
}

} // namespace vsr::scivis_studio
