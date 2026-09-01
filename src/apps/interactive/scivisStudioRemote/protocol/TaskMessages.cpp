// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TaskMessages.h"
#include "PayloadCommon.h"

namespace vsr::scivis_studio::protocol {

void toNode(const TaskStartedResult &r, vsr::core::DataNode &n)
{
  writeChild(n, "taskId", r.taskId);
}

bool fromNode(const vsr::core::DataNode &n, TaskStartedResult &r)
{
  return readChild(n, "taskId", r.taskId);
}

void toNode(const TaskProgress &p, vsr::core::DataNode &n)
{
  writeChild(n, "taskId", p.taskId);
  writeChild(n, "current", p.current);
  writeChild(n, "total", p.total);
  writeChild(n, "message", p.message);
}

bool fromNode(const vsr::core::DataNode &n, TaskProgress &p)
{
  if (!readChild(n, "taskId", p.taskId))
    return false;
  p.current = readChildOr(n, "current", uint64_t(0));
  p.total = readChildOr(n, "total", uint64_t(0));
  p.message = readChildOr(n, "message", std::string());
  return true;
}

void toNode(const TaskCompleted &c, vsr::core::DataNode &n)
{
  writeChild(n, "taskId", c.taskId);
  writeChild(n, "message", c.message);
  writeChild(n, "framesCompleted", c.framesCompleted);
}

bool fromNode(const vsr::core::DataNode &n, TaskCompleted &c)
{
  if (!readChild(n, "taskId", c.taskId))
    return false;
  c.message = readChildOr(n, "message", std::string());
  c.framesCompleted = readChildOr(n, "framesCompleted", uint64_t(0));
  return true;
}

void toNode(const TaskFailed &f, vsr::core::DataNode &n)
{
  writeChild(n, "taskId", f.taskId);
  writeChild(n, "error", f.error);
}

bool fromNode(const vsr::core::DataNode &n, TaskFailed &f)
{
  if (!readChild(n, "taskId", f.taskId))
    return false;
  f.error = readChildOr(n, "error", std::string());
  return true;
}

void toNode(const CancelTask &c, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", c.requestId);
  writeChild(n, "taskId", c.taskId);
}

bool fromNode(const vsr::core::DataNode &n, CancelTask &c)
{
  return readChild(n, "requestId", c.requestId)
      && readChild(n, "taskId", c.taskId);
}

} // namespace vsr::scivis_studio::protocol
