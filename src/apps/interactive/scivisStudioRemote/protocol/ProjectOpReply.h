// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <optional>
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Uniform server->client answer to every project request (ADR 0034). The
 * requestId echoes the client-minted id of the request being answered; a
 * failed op carries `ok == false` and a user-facing `error`. `results` is an
 * opaque subtree the sender fills with an op-specific *Result payload (newly
 * allocated ids, a started task id, ...) and the receiver decodes with
 * results<R>(); it is null when the op has nothing to return.
 *
 * Example:
 *   auto reply = makeOkReply(request.requestId);
 *   setResults(reply, TaskStartedResult{taskId});
 *   channel.send(encode(reply));
 *   ...
 *   if (auto started =
 * results<TaskStartedResult>(*decode<ProjectOpReply>(msg)))
 *     track(started->taskId);
 */
struct ProjectOpReply
{
  static constexpr StudioMessageType kType = StudioMessageType::ProjectOpReply;
  uint64_t requestId{0};
  bool ok{true};
  std::string error;
  SubtreePtr results;
};

// requestId and ok are required; error and results are optional.
void toNode(const ProjectOpReply &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ProjectOpReply &);

// Replaces reply.results with a fresh subtree holding toNode(result).
template <typename R>
void setResults(ProjectOpReply &reply, const R &result);

// Decodes reply.results as an R; empty when there are no results or
// fromNode() rejects them.
template <typename R>
std::optional<R> results(const ProjectOpReply &reply);

ProjectOpReply makeOkReply(uint64_t requestId);
ProjectOpReply makeErrorReply(uint64_t requestId, std::string error);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename R>
inline void setResults(ProjectOpReply &reply, const R &result)
{
  reply.results = makeSubtree();
  toNode(result, reply.results->root());
}

template <typename R>
inline std::optional<R> results(const ProjectOpReply &reply)
{
  if (!reply.results)
    return {};
  R result;
  if (!fromNode(reply.results->root(), result))
    return {};
  return result;
}

} // namespace vsr::scivis_studio::protocol
