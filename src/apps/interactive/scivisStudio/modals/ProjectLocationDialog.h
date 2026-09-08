// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_modals
#include "modals/BrowseProvider.h"
#include "modals/ModalAction.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <filesystem>
#include <memory>
#include <string>

namespace vsr::scivis_studio::modals {

enum class ProjectLocationMode
{
  OpenProject,
  SaveProjectAs
};

/*
 * Where to open a project from or save it to: a free-text directory plus
 * Browse (the host's provider in directory mode). Whether the directory
 * holds a project, or already holds one when saving, is the Action's
 * business -- the monolith answers from its own filesystem, the client from
 * the server's reply -- and either way the dialog stays open showing that
 * error, closing only when the action accepts.
 *
 * Example:
 *   m_projectLocationDialog->configure(ProjectLocationMode::OpenProject);
 *   m_projectLocationDialog->show();
 */
struct ProjectLocationDialog : public vsr::ui::imgui::Modal
{
  struct Request
  {
    ProjectLocationMode mode{ProjectLocationMode::OpenProject};
    std::filesystem::path directory;
  };

  struct Action : public ModalAction
  {
    virtual void submit(const Request &request, ActionResult done) = 0;
    // What the directory field starts at when the dialog is configured;
    // empty for a host with nothing to propose.
    virtual std::string initialDirectory(ProjectLocationMode mode) const = 0;
  };

  ProjectLocationDialog(vsr::ui::imgui::Application *app,
      std::unique_ptr<BrowseProvider> browse,
      std::unique_ptr<Action> action);
  ~ProjectLocationDialog() override;

  void configure(ProjectLocationMode mode);

 private:
  void buildUI() override;
  void submit();
  void reset();

  std::unique_ptr<BrowseProvider> m_browse;
  std::unique_ptr<Action> m_action;
  ProjectLocationMode m_mode{ProjectLocationMode::OpenProject};
  std::string m_directory;
  std::string m_error;
};

} // namespace vsr::scivis_studio::modals
