// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioProtocol.h"

namespace vsr::scivis_studio::protocol {

const char *toString(StudioMessageType type)
{
  const auto *row = findMessageType(type);
  return row ? row->name : "Unknown";
}

} // namespace vsr::scivis_studio::protocol
