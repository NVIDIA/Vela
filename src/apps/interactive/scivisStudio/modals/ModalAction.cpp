// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ModalAction.h"
// vsr_scivis_studio_modals
#include "modals/ModalUI.h"
// imgui
#include <imgui.h>

namespace vsr::scivis_studio::modals {

ModalAction::~ModalAction() = default;

bool ModalAction::busy() const
{
  return false;
}

bool ModalAction::canSubmit() const
{
  return true;
}

const char *ModalAction::busyMessage() const
{
  return "working...";
}

void ModalAction::reset() {}

ModalChoice modalFooter(BrowseProvider &browse,
    const ModalAction &action,
    const std::string &error,
    const char *actionLabel,
    bool submitNow)
{
  const bool busy = action.busy();
  if (busy)
    ImGui::TextDisabled("%s", action.busyMessage());
  errorText(error);

  // Read before the browse draws: a provider with its own window takes
  // Escape in the same frame, which must not count as the modal's too.
  const bool browseWasVisible = browse.visible();
  browse.renderUI();

  ImGui::Spacing();
  if (ImGui::Button("Cancel")
      || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !browseWasVisible))
    return ModalChoice::Cancelled;

  ImGui::SameLine();
  ModalChoice choice = ModalChoice::None;
  ImGui::BeginDisabled(busy || !action.canSubmit());
  if (ImGui::Button(actionLabel) || (submitNow && !busy))
    choice = ModalChoice::Submitted;
  ImGui::EndDisabled();
  return choice;
}

} // namespace vsr::scivis_studio::modals
