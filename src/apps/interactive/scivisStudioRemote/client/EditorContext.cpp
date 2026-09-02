// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "EditorContext.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::scivis_studio::client {

const Project *EditorContext::project() const
{
  return connection ? connection->project() : nullptr;
}

ProjectOps &EditorContext::ops() const
{
  return connection->projectOps();
}

bool EditorContext::canSend() const
{
  return connection && connection->state() == ConnectionState::Connected
      && !connection->bootstrapping() && connection->project() != nullptr;
}

bool EditorContext::renderInProgress() const
{
  return connection && connection->projectOps().renderActive();
}

ReplyCallback EditorContext::errorReporter() const
{
  return [this](const protocol::ProjectOpReply &reply) {
    if (!reply.ok)
      error(reply.error);
  };
}

void EditorContext::error(const std::string &message) const
{
  if (reportError)
    reportError(message);
  else
    vsr::core::logError("[Client] %s", message.c_str());
}

void EditorContext::status(const std::string &message) const
{
  if (reportStatus)
    reportStatus(message);
  else
    vsr::core::logStatus("[Client] %s", message.c_str());
}

} // namespace vsr::scivis_studio::client
