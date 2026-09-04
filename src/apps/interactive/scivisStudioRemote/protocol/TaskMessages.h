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
 * Server Task payloads (ADR 0032). A task-launching request is answered by a
 * ProjectOpReply whose results decode to TaskStartedResult; the server then
 * pushes any number of TaskProgress events and exactly one of TaskCompleted /
 * TaskFailed, all carrying the server-allocated taskId. CancelTask is the
 * client's cooperative cancel request.
 *
 * Example:
 *   TaskProgress p;
 *   p.taskId = id;
 *   p.current = 3;
 *   p.total = 10;
 *   channel.send(encode(p));
 */

// Result payload of a task-launching request; never travels alone.
struct TaskStartedResult
{
  uint64_t taskId{0};
};

struct TaskProgress
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::TaskProgress;
  uint64_t taskId{0};
  uint64_t current{0};
  uint64_t total{0}; // 0 = indeterminate
  std::string message;
};

struct TaskCompleted
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::TaskCompleted;
  uint64_t taskId{0};
  std::string message;
  uint64_t framesCompleted{0}; // RenderShot only; 0 otherwise
};

struct TaskFailed
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::TaskFailed;
  uint64_t taskId{0};
  std::string error;
  // RenderShot only: the frames written before the cancel or failure; they
  // stay on disk. 0 otherwise.
  uint64_t framesCompleted{0};
};

struct CancelTask
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::CancelTask;
  uint64_t requestId{0};
  uint64_t taskId{0};
};

// Every payload is a fields() description (PayloadCommon.h). taskId is
// required in every task payload and requestId in CancelTask;
// current/total/framesCompleted default to 0 and message/error to "" when
// absent.

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, TaskStartedResult &r)
{
  v.required("taskId", r.taskId);
}

template <typename V>
void fields(V &v, TaskProgress &p)
{
  v.required("taskId", p.taskId);
  v.optional("current", p.current);
  v.optional("total", p.total);
  v.optional("message", p.message);
}

template <typename V>
void fields(V &v, TaskCompleted &c)
{
  v.required("taskId", c.taskId);
  v.optional("message", c.message);
  v.optional("framesCompleted", c.framesCompleted);
}

template <typename V>
void fields(V &v, TaskFailed &f)
{
  v.required("taskId", f.taskId);
  v.optional("error", f.error);
  v.optional("framesCompleted", f.framesCompleted);
}

template <typename V>
void fields(V &v, CancelTask &c)
{
  v.required("requestId", c.requestId);
  v.required("taskId", c.taskId);
}

} // namespace vsr::scivis_studio::protocol
