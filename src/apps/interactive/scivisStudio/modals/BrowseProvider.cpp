// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "BrowseProvider.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"

namespace vsr::scivis_studio::modals {

BrowseProvider::~BrowseProvider() = default;

NativeBrowseProvider::NativeBrowseProvider(vsr::ui::imgui::Application *app)
    : m_app(app)
{}

NativeBrowseProvider::~NativeBrowseProvider() = default;

void NativeBrowseProvider::browse(BrowseRequest request)
{
  m_request = std::move(request);
  m_path.clear();
  m_paths.clear();
  m_browsing = true;

  using vsr::ui::imgui::FileDialogMode;
  switch (m_request.mode) {
  case BrowseMode::OpenFiles:
    m_app->getFilenamesFromDialog(m_paths);
    break;
  case BrowseMode::SaveFile:
    m_app->getFilenameFromDialog(m_path, FileDialogMode::SaveFile);
    break;
  case BrowseMode::OpenDirectory:
    m_app->getFilenameFromDialog(m_path, FileDialogMode::OpenDirectory);
    break;
  case BrowseMode::OpenFile:
    m_app->getFilenameFromDialog(m_path, FileDialogMode::OpenFile);
    break;
  }
}

void NativeBrowseProvider::renderUI()
{
  // A cancelled dialog writes nothing, so a browse that never answers simply
  // stays pending until the next one replaces it.
  if (!m_browsing)
    return;

  std::vector<std::filesystem::path> chosen;
  if (!m_path.empty())
    chosen.emplace_back(m_path);
  for (const auto &path : m_paths)
    chosen.emplace_back(path);
  if (chosen.empty())
    return;

  m_browsing = false;
  m_path.clear();
  m_paths.clear();
  if (m_request.onAccept)
    m_request.onAccept(chosen);
}

bool NativeBrowseProvider::visible() const
{
  return false;
}

} // namespace vsr::scivis_studio::modals
