// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <optional>
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

/*
 * The last message on a connection its sender is about to close. A client's
 * courtesy Disconnect carries no reason; the server's farewell says why it
 * ends the session (an evicted client shows it in its banner).
 */
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

// Serialization //////////////////////////////////////////////////////////////

// Every payload is a fields() description (PayloadCommon.h). version is
// required; buildInfo, message and reason are optional and read back as ""
// when absent. The bracket and liveness payloads carry nothing.

// The reason a received farewell gives (pass it decode<Disconnect>()'s
// result), or a stand-in when it gives none or did not decode.
std::string farewellReason(const std::optional<Disconnect> &farewell);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, Hello &h)
{
  v.required("version", h.version);
  v.optional("buildInfo", h.buildInfo);
}

template <typename V>
void fields(V &v, Error &e)
{
  v.optional("message", e.message);
}

template <typename V>
void fields(V &v, Disconnect &d)
{
  v.optional("reason", d.reason);
}

template <typename V>
void fields(V &, Ping &)
{}

template <typename V>
void fields(V &, Pong &)
{}

template <typename V>
void fields(V &, Shutdown &)
{}

template <typename V>
void fields(V &, BootstrapBegin &)
{}

template <typename V>
void fields(V &, BootstrapEnd &)
{}

} // namespace vsr::scivis_studio::protocol
