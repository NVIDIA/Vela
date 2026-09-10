// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <string>
#include <vector>

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
 * SetObjectMetadata carries a run of an object's metadata keys, each with the
 * Any the key now holds; an entry with no value means the key was removed.
 * One message per key would be six per frame of a camera orbit, so a
 * metadata batch travels as one message (ADR 0036). Array-valued metadata
 * (a volume's opacityControlPoints) never rides it, for the same reason
 * arrays never ride SetObjectParameter.
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

// An invalid `value` is a removal: the key is gone from the object.
struct ObjectMetadataEntry
{
  std::string name;
  vsr::core::Any value;
};

struct SetObjectMetadata
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetObjectMetadata;
  SceneObjectRef object;
  std::vector<ObjectMetadataEntry> entries;
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

// An entry's name is required and its value optional (absent is a removal);
// an array value is rejected. SetObjectMetadata's object is required and its
// entry list may be empty (nothing to apply).
void toNode(const ObjectMetadataEntry &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ObjectMetadataEntry &);

// The other three are fields() descriptions (PayloadCommon.h); every field
// is required, and SetObjectMetadata's entry list reads its items through the
// codec above.

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, RemoveObjectParameter &p)
{
  v.child("object", p.object);
  v.required("name", p.name);
}

template <typename V>
void fields(V &v, SetObjectMetadata &m)
{
  v.child("object", m.object);
  v.list("entries", m.entries);
}

template <typename V>
void fields(V &v, SetNodeTransform &t)
{
  v.child("node", t.node);
  v.required("transform", t.transform);
}

} // namespace vsr::scivis_studio::protocol
