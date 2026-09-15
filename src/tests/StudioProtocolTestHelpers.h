// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
#include "StudioCodec.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>

/*
 * Round trips for the Studio payload tests. Payload structs have no
 * operator==, so a scenario compares the copy field by field; these only
 * take the encode-decode step out of the way and fail the test when the
 * copy does not decode at all.
 *
 * Example:
 *   const auto out = roundTrip(request);
 *   REQUIRE(out.requestId == request.requestId);
 */

// Encodes a message-level payload, checks the wire type is its own, decodes
// it back and hands back the copy.
template <typename T>
T roundTrip(const T &payload)
{
  const auto msg = vsr::scivis_studio::protocol::encode(payload);
  REQUIRE(msg.header.type == uint8_t(T::MESSAGE_TYPE));
  const auto out = vsr::scivis_studio::protocol::decode<T>(msg);
  REQUIRE(out);
  return *out;
}

// Result payloads never travel alone (no MESSAGE_TYPE), so they round-trip
// through a serialized DataTree, decoded into `into`: preload it to show
// the decode replaces what was there rather than adding to it.
template <typename T>
T roundTripTree(const T &payload, T into = {})
{
  vsr::core::DataTree tree;
  toNode(payload, tree.root());
  vsr::network::MessagePayload bytes;
  tree.write(bytes);
  vsr::core::DataTree copy;
  REQUIRE(copy.read(bytes));
  REQUIRE(fromNode(copy.root(), into));
  return into;
}
