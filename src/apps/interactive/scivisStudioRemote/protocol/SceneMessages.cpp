// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SceneMessages.h"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::scivis_studio::protocol {

vsr::network::Message encodeSceneMessage(
    vsr::network::StructuredMessage &message, StudioMessageType type)
{
  if (!isSceneMessageType(type)) {
    vsr::core::logError(
        "[StudioProtocol] encodeSceneMessage: '%s' is not a scene message",
        toString(type));
    return {};
  }
  return message.toMessage(uint8_t(type));
}

} // namespace vsr::scivis_studio::protocol
