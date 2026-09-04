// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOpReply.h"

namespace vsr::scivis_studio::protocol {

ProjectOpReply makeOkReply(uint64_t requestId)
{
  ProjectOpReply reply;
  reply.requestId = requestId;
  reply.ok = true;
  return reply;
}

ProjectOpReply makeErrorReply(uint64_t requestId, std::string error)
{
  ProjectOpReply reply;
  reply.requestId = requestId;
  reply.ok = false;
  reply.error = std::move(error);
  return reply;
}

} // namespace vsr::scivis_studio::protocol
