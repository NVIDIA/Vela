// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StudioProtocol.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Session-level payloads: handshake, liveness, teardown and the bootstrap
 * brackets that frame the initial state transfer after connect.
 */

struct Hello
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::Hello;
  int version{PROTOCOL_VERSION};
  std::string buildInfo;
};

struct Error
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::Error;
  std::string message;
};

struct Ping
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::Ping;
};

struct Pong
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::Pong;
};

// The last message on a connection its sender is about to close. A client's
// courtesy Disconnect carries no reason; the server's farewell says why it
// ends the session (an evicted client shows it in its banner).
struct Disconnect
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::Disconnect;
  std::string reason;
};

struct Shutdown
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::Shutdown;
};

struct BootstrapBegin
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::BootstrapBegin;
};

struct BootstrapEnd
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::BootstrapEnd;
};

// version is required; buildInfo is optional.
void toNode(const Hello &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Hello &);

// message is optional (an empty message reads back as "").
void toNode(const Error &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Error &);

// reason is optional (an empty reason reads back as "").
void toNode(const Disconnect &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Disconnect &);

// Empty payloads: toNode writes nothing, fromNode always succeeds.
void toNode(const Ping &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Ping &);
void toNode(const Pong &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Pong &);
void toNode(const Shutdown &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Shutdown &);
void toNode(const BootstrapBegin &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, BootstrapBegin &);
void toNode(const BootstrapEnd &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, BootstrapEnd &);

} // namespace vsr::scivis_studio::protocol
