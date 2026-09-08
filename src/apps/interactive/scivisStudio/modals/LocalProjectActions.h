// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudio
#include "ProjectContext.h"
#include "modals/AddFileAnimationDatasetDialog.h"
#include "modals/AddStaticDatasetDialog.h"
#include "modals/ProjectLocationDialog.h"
// std
#include <functional>

namespace vsr::ui::imgui {
class Application;
}

namespace vsr::scivis_studio {

/*
 * The monolith's half of the shared modals: it owns the project, so every
 * request is answered in place -- the import runs behind the Application's
 * task modal, and a directory is checked against the local filesystem. The
 * remote client's half is scivisStudioClient's RemoteProjectActions, which
 * answers the same requests from the server's replies.
 *
 * Example:
 *   auto dialog = std::make_unique<modals::AddStaticDatasetDialog>(this,
 *       std::make_unique<modals::NativeBrowseProvider>(this),
 *       std::make_unique<LocalStaticDatasetAction>(this, &m_projectContext));
 */

struct LocalStaticDatasetAction : public modals::AddStaticDatasetDialog::Action
{
  LocalStaticDatasetAction(
      vsr::ui::imgui::Application *app, ProjectContext *projectContext);
  ~LocalStaticDatasetAction() override;

  void submit(const modals::AddStaticDatasetDialog::Request &request,
      modals::ActionResult done) override;

 private:
  vsr::ui::imgui::Application *m_app{nullptr};
  ProjectContext *m_projectContext{nullptr};
};

struct LocalFileAnimationAction
    : public modals::AddFileAnimationDatasetDialog::Action
{
  LocalFileAnimationAction(
      vsr::ui::imgui::Application *app, ProjectContext *projectContext);
  ~LocalFileAnimationAction() override;

  void submit(const modals::AddFileAnimationDatasetDialog::Request &request,
      modals::ActionResult done) override;

 private:
  vsr::ui::imgui::Application *m_app{nullptr};
  ProjectContext *m_projectContext{nullptr};
};

struct LocalProjectLocationAction : public modals::ProjectLocationDialog::Action
{
  // `accept` opens or saves the project at a directory this action already
  // validated.
  using Accept = std::function<void(
      modals::ProjectLocationMode, const std::filesystem::path &)>;

  explicit LocalProjectLocationAction(Accept accept);
  ~LocalProjectLocationAction() override;

  void submit(const modals::ProjectLocationDialog::Request &request,
      modals::ActionResult done) override;
  std::string initialDirectory(modals::ProjectLocationMode mode) const override;

 private:
  Accept m_accept;
};

} // namespace vsr::scivis_studio
