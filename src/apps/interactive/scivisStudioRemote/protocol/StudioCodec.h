// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/Message.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <optional>
#include <type_traits>

namespace vsr::scivis_studio::protocol {

/*
 * Generic codec between payload structs and wire Messages. A payload type T
 * must expose `static constexpr StudioMessageType MESSAGE_TYPE` and the free
 * functions `toNode(const T &, DataNode &)` / `fromNode(const DataNode &, T
 * &)`. Frames are the one payload that bypasses this (raw bytes, see
 * FrameMessages.h); everything else, including empty payloads, goes through
 * a DataTree.
 *
 * Example:
 *   auto msg = encode(Hello{});
 *   if (auto hello = decode<Hello>(msg))
 *     use(hello->version);
 */

// Serializes `payload` into a Message tagged with T::MESSAGE_TYPE.
template <typename T>
vsr::network::Message encode(const T &payload);

// Empty when the type byte is not T::MESSAGE_TYPE, the tree fails to parse, or
// fromNode() rejects the contents. Never throws on malformed input.
template <typename T>
std::optional<T> decode(const vsr::network::Message &msg);

// The message's type if it belongs to the Studio set, empty otherwise, so a
// receiver can reject unknown types instead of silently dropping them.
std::optional<StudioMessageType> messageType(const vsr::network::Message &msg);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline vsr::network::Message encode(const T &payload)
{
  static_assert(std::is_same_v<std::decay_t<decltype(T::MESSAGE_TYPE)>,
                    StudioMessageType>,
      "protocol payloads must declare `static constexpr StudioMessageType "
      "MESSAGE_TYPE`");
  vsr::core::DataTree tree;
  toNode(payload, tree.root());
  vsr::network::Message msg;
  msg.header.type = uint8_t(T::MESSAGE_TYPE);
  tree.write(msg.payload);
  msg.header.payload_length = uint32_t(msg.payload.size());
  return msg;
}

template <typename T>
inline std::optional<T> decode(const vsr::network::Message &msg)
{
  static_assert(std::is_same_v<std::decay_t<decltype(T::MESSAGE_TYPE)>,
                    StudioMessageType>,
      "protocol payloads must declare `static constexpr StudioMessageType "
      "MESSAGE_TYPE`");
  if (msg.header.type != uint8_t(T::MESSAGE_TYPE))
    return {};
  vsr::core::DataTree tree;
  // A bare message with no bytes at all is accepted as an empty tree so that
  // header-only sends (makeMessage(type)) still decode for empty payloads.
  if (!msg.payload.empty() && !tree.read(msg.payload))
    return {};
  T payload;
  if (!fromNode(tree.root(), payload))
    return {};
  return payload;
}

inline std::optional<StudioMessageType> messageType(
    const vsr::network::Message &msg)
{
  if (!isStudioMessageType(msg.header.type))
    return {};
  return StudioMessageType(msg.header.type);
}

} // namespace vsr::scivis_studio::protocol
