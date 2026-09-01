// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Optimistic scene edits: client->server, one-way, latest-wins, no reply.
 * Objects are addressed by their wire identity (SceneObjectRef) and rig-owned
 * transform nodes by SceneNodeRef.
 *
 * SetObjectParameter carries a single vsr::core::Any stored exactly as a
 * DataNode stores a value, so every scalar, string, vector, matrix or object
 * reference the Any supports round-trips with its ANARI type intact. Array
 * data never rides this message: dataset arrays are server-resident and a
 * node holding an array is rejected on read.
 *
 * Example:
 *   SetObjectParameter edit;
 *   edit.object = ref;
 *   edit.name = "radius";
 *   edit.value = vsr::core::Any(0.5f);
 *   channel.send(encode(edit));
 */

struct SetObjectParameter
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetObjectParameter;
  SceneObjectRef object;
  std::string name;
  vsr::core::Any value;
};

struct RemoveObjectParameter
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveObjectParameter;
  SceneObjectRef object;
  std::string name;
};

struct SetNodeTransform
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetNodeTransform;
  SceneNodeRef node;
  vsr::math::mat4 transform{vsr::math::IDENTITY_MAT4};
};

// object, name and a non-array, non-empty value are required.
void toNode(const SetObjectParameter &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetObjectParameter &);

// object and name are required.
void toNode(const RemoveObjectParameter &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveObjectParameter &);

// node and transform are required.
void toNode(const SetNodeTransform &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetNodeTransform &);

} // namespace vsr::scivis_studio::protocol
