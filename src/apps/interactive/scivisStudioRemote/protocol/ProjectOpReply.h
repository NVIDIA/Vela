// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Uniform server->client answer to every project request (ADR 0034). The
 * requestId echoes the client-minted id of the request being answered; a
 * failed op carries `ok == false` and a user-facing `error`. `results` is an
 * opaque subtree the sender fills with an op-specific *Result payload (newly
 * allocated ids, a started task id, ...) and the receiver decodes with
 * results<R>() (PayloadCommon.h); it is null when the op has nothing to
 * return.
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
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ProjectOpReply;
  uint64_t requestId{0};
  bool ok{true};
  std::string error;
  SubtreePtr results;
};

// The reply is a fields() description (PayloadCommon.h): requestId and ok
// are required; error and results are optional.

ProjectOpReply makeOkReply(uint64_t requestId);
ProjectOpReply makeErrorReply(uint64_t requestId, std::string error);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, ProjectOpReply &r)
{
  v.required("requestId", r.requestId);
  v.required("ok", r.ok);
  v.optional("error", r.error);
  v.subtree("results", r.results);
}

} // namespace vsr::scivis_studio::protocol
