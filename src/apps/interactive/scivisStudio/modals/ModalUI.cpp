// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ModalUI.h"

namespace vsr::scivis_studio::modals {

namespace {

// TextColored does not wrap and TextWrapped does not colour, so the two are
// combined by hand: a path or a host's error is long enough to need both.
void coloredWrappedText(const ImVec4 &color, const std::string &text)
{
  if (text.empty())
    return;
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

} // namespace

void errorText(const std::string &text)
{
  coloredWrappedText(ERROR_TEXT_COLOR, text);
}

void warningText(const std::string &text)
{
  coloredWrappedText(WARNING_TEXT_COLOR, text);
}

std::vector<std::string> archiveExtensions()
{
  return {ARCHIVE_EXTENSIONS.begin(), ARCHIVE_EXTENSIONS.end()};
}

} // namespace vsr::scivis_studio::modals
