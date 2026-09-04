// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/Message.hpp"

namespace vsr::scivis_studio::protocol {

/*
 * Scene server->client messages. Studio defines no payload structs of its own
 * here: the structural scene transfer reuses the vsr::network::messages
 * classes verbatim and only re-tags them with a Studio type value, so both
 * sides keep sharing one serializer with the existing render server.
 *
 *   StudioMessageType::TransferScene  <-> messages::TransferScene
 *       (sender built with includeArrayData = false: descriptor-only arrays)
 *   StudioMessageType::TransferLayer  <-> messages::TransferLayer
 *   StudioMessageType::ObjectAdded    <-> messages::NewObject
 *   StudioMessageType::ObjectRemoved  <-> messages::RemoveObject
 *
 * The receiver constructs the paired messages class from the Message and a
 * target Scene, then calls execute().
 *
 * Example:
 *   vsr::network::messages::TransferLayer layer(&scene, l);
 *   channel.send(encodeSceneMessage<StudioMessageType::TransferLayer>(layer));
 *   ...
 *   if (messageType(msg) == StudioMessageType::TransferLayer)
 *     vsr::network::messages::TransferLayer(msg, &replica).execute();
 */

// True for the four scene values listed above.
constexpr bool isSceneMessageType(StudioMessageType type);

// Re-tags an existing vsr::network StructuredMessage (TransferScene,
// TransferLayer, NewObject, RemoveObject) with one of the four Studio scene
// type values; any other TYPE is a compile error.
template <StudioMessageType TYPE>
vsr::network::Message encodeSceneMessage(
    vsr::network::StructuredMessage &message);

// Inlined definitions ////////////////////////////////////////////////////////

constexpr bool isSceneMessageType(StudioMessageType type)
{
  switch (type) {
  case StudioMessageType::TransferScene:
  case StudioMessageType::TransferLayer:
  case StudioMessageType::ObjectAdded:
  case StudioMessageType::ObjectRemoved:
    return true;
  default:
    return false;
  }
}

template <StudioMessageType TYPE>
inline vsr::network::Message encodeSceneMessage(
    vsr::network::StructuredMessage &message)
{
  static_assert(isSceneMessageType(TYPE),
      "encodeSceneMessage() carries only the four scene message types");
  return message.toMessage(uint8_t(TYPE));
}

} // namespace vsr::scivis_studio::protocol
