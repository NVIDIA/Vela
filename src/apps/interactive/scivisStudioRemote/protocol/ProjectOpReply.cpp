// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOpReply.h"

namespace vsr::scivis_studio::protocol {

void toNode(const ProjectOpReply &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "ok", r.ok);
  writeChild(n, "error", r.error);
  writeSubtree(n, "results", r.results);
}

bool fromNode(const vsr::core::DataNode &n, ProjectOpReply &r)
{
  if (!readChild(n, "requestId", r.requestId) || !readChild(n, "ok", r.ok))
    return false;
  r.error = readChildOr(n, "error", std::string());
  r.results = readSubtree(n, "results");
  return true;
}

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
