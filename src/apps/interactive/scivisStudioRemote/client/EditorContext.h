// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client_core
#include "ProjectOps.h"
#include "ServerConnection.h"
// std
#include <functional>
#include <string>

namespace vsr::scivis_studio {
struct Project;
}

namespace vsr::scivis_studio::client {

/*
 * What every adapted editor and modal needs from the application: the
 * connection (Project Replica in, Project Ops out), where to surface errors
 * and status, and the project-level actions the menus also offer. Errors
 * from replies go to the Log window and a transient toast, never a modal
 * that blocks. Owned by the Application and outlives every window.
 *
 * Example:
 *   if (m_context->canSend())
 *     m_context->ops().removeShot(shotId, m_context->errorReporter());
 */
struct EditorContext
{
  ServerConnection *connection{nullptr};
  std::function<void(const std::string &)> reportError;
  std::function<void(const std::string &)> reportStatus;

  // Project-level actions the Project window shares with the File menu.
  struct Actions
  {
    std::function<void()> newProject;
    std::function<void()> openProject;
    std::function<void()> saveProject;
    std::function<void()> saveProjectAs;
  } actions;

  // The replica; null before the first snapshot and after disconnect().
  const Project *project() const;
  ProjectOps &ops() const;
  // Connected, bootstrapped and holding a replica: the only time an editor
  // may send. Lost and bootstrapping leave the panels read-only.
  bool canSend() const;
  // A ReplyCallback that reports a failed reply's error and nothing else.
  ReplyCallback errorReporter() const;
  void error(const std::string &message) const;
  void status(const std::string &message) const;
};

} // namespace vsr::scivis_studio::client
