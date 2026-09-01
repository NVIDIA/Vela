// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SessionMessages.h"
#include "PayloadMacros.h"

namespace vsr::scivis_studio::protocol {

void toNode(const Hello &h, vsr::core::DataNode &n)
{
  writeChild(n, "version", h.version);
  writeChild(n, "buildInfo", h.buildInfo);
}

bool fromNode(const vsr::core::DataNode &n, Hello &h)
{
  if (!readChild(n, "version", h.version))
    return false;
  h.buildInfo = readChildOr(n, "buildInfo", std::string());
  return true;
}

void toNode(const Error &e, vsr::core::DataNode &n)
{
  writeChild(n, "message", e.message);
}

bool fromNode(const vsr::core::DataNode &n, Error &e)
{
  e.message = readChildOr(n, "message", std::string());
  return true;
}

// Empty payloads /////////////////////////////////////////////////////////////

VSR_STUDIO_EMPTY_PAYLOAD(Ping)
VSR_STUDIO_EMPTY_PAYLOAD(Pong)
VSR_STUDIO_EMPTY_PAYLOAD(Disconnect)
VSR_STUDIO_EMPTY_PAYLOAD(Shutdown)
VSR_STUDIO_EMPTY_PAYLOAD(BootstrapBegin)
VSR_STUDIO_EMPTY_PAYLOAD(BootstrapEnd)

} // namespace vsr::scivis_studio::protocol
