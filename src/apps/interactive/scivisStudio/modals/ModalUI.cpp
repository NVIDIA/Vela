// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ModalUI.h"

namespace vsr::scivis_studio::modals {

void errorText(const std::string &text)
{
  if (!text.empty())
    ImGui::TextColored(ERROR_TEXT_COLOR, "%s", text.c_str());
}

void warningText(const std::string &text)
{
  if (!text.empty())
    ImGui::TextColored(WARNING_TEXT_COLOR, "%s", text.c_str());
}

std::vector<std::string> archiveExtensions()
{
  return {ARCHIVE_EXTENSIONS.begin(), ARCHIVE_EXTENSIONS.end()};
}

} // namespace vsr::scivis_studio::modals
