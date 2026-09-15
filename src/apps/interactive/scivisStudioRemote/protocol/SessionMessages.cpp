// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SessionMessages.h"

namespace vsr::scivis_studio::protocol {

std::string farewellReason(const std::optional<Disconnect> &farewell)
{
  if (farewell && !farewell->reason.empty())
    return farewell->reason;
  return "server closed the connection";
}

} // namespace vsr::scivis_studio::protocol
