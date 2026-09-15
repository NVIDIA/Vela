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
 * does ride it (v12): a volume's opacityControlPoints is the durable half of
 * its Transfer Function, it is small, and unlike a dataset's arrays it is
 * authored on the client. Bulk array *objects* still never ride here -- they
 * have their own message, SetArrayData.
 *
 * SetArrayData is not declared in this header. It re-tags the existing
 * vsr::network::messages::TransferArrayData with a Studio type value, the way
 * SceneMessages.h re-tags the scene transfers, so both ends share one
 * serializer and the proxy-fill and element-type/size guards that come with
 * it. It is the client-to-server half; the server-to-client half is a reply
 * (RequestArrayData / ArrayDataResult in ViewportMessages.h), because the
 * client asks for samples rather than being pushed them.
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

// One metadata key in one of its three states. A key holding array data --
// a volume's opacityControlPoints is the whole of v12's interest in it --
// carries `arrayElementType` and its bytes; a key holding a single value
// carries `value`; an entry with neither is a removal. `value` and the array
// fields are never both set.
struct ObjectMetadataEntry
{
  std::string name;
  vsr::core::Any value;
  anari::DataType arrayElementType{ANARI_UNKNOWN};
  uint64_t arrayElementCount{0};
  std::vector<std::byte> arrayData;

  bool holdsArray() const { return arrayElementType != ANARI_UNKNOWN; }
  bool isRemoval() const { return !holdsArray() && !value.valid(); }
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

// An entry's name is required. `arrayElementType`, when present, makes the
// entry an array one and its bytes are read from the `array` leaf beside it
// (absent leaf reads as empty); otherwise `value` is optional and its absence
// is a removal. An entry carrying both an array type and a value is rejected.
// SetObjectMetadata's object is required and its entry list may be empty.
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
