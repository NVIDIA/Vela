// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StudioProtocol.h"
// std
#include <charconv>

namespace vsr::scivis_studio::protocol {

bool parsePort(const std::string &text, int &port)
{
  int value = 0;
  const auto *begin = text.data();
  const auto *end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return false;
  if (value < 1 || value > 65535)
    return false;
  port = value;
  return true;
}

const char *toString(StudioMessageType type)
{
  const auto *row = findMessageType(type);
  return row ? row->name : "Unknown";
}

} // namespace vsr::scivis_studio::protocol
