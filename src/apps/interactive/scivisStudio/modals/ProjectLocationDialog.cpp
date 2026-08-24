// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectLocationDialog.h"

#include "ProjectSerialization.h"

#include "vsr/ui/imgui/Application.h"

#include "imgui.h"

#include <cstring>

namespace vsr::scivis_studio {

namespace {

template <size_t N>
void copyToInputBuffer(std::array<char, N> &buffer, const std::string &value)
{
  buffer.fill('\0');
  std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
}

} // namespace

ProjectLocationDialog::ProjectLocationDialog(vsr::ui::imgui::Application *app)
    : Modal(app, "Project Location")
{}

ProjectLocationDialog::~ProjectLocationDialog() = default;

void ProjectLocationDialog::configure(ProjectLocationMode mode,
    std::function<void(const std::filesystem::path &)> onAccept)
{
  m_mode = mode;
  m_onAccept = std::move(onAccept);
  m_error.clear();
}

bool ProjectLocationDialog::validate(
    const std::filesystem::path &path, std::string &error) const
{
  if (path.empty()) {
    error = "Enter a project directory.";
    return false;
  }

  const auto manifest =
      resolveProjectFileForRead(path / PROJECT_MANIFEST_FILENAME);
  if (m_mode == ProjectLocationMode::OpenProject) {
    auto result = validateProjectRoot(path);
    if (!result.ok)
      error = result.error;
    return result.ok;
  }

  if (std::filesystem::exists(manifest)) {
    error = "Target directory already contains " + manifest.filename().string()
        + ".";
    return false;
  }

  if (std::filesystem::exists(path) && !std::filesystem::is_directory(path)) {
    error = "Target path is not a directory.";
    return false;
  }

  return true;
}

void ProjectLocationDialog::buildUI()
{
  const char *title = "Open Project";
  const char *button = "Open";
  if (m_mode == ProjectLocationMode::SaveProjectAs) {
    title = "Save Project As";
    button = "Save";
  }

  ImGui::TextUnformatted(title);

  if (!m_browsedDirectory.empty()) {
    copyToInputBuffer(m_directory, m_browsedDirectory);
    m_browsedDirectory.clear();
  }

  if (ImGui::Button("...##projectDirectory")) {
    m_browsedDirectory.clear();
    m_app->getFilenameFromDialog(
        m_browsedDirectory, vsr::ui::imgui::FileDialogMode::OpenDirectory);
  }
  ImGui::SameLine();
  ImGui::InputText("Directory", m_directory.data(), m_directory.size());
  if (!m_error.empty())
    ImGui::TextColored(ImVec4(1.f, 0.35f, 0.25f, 1.f), "%s", m_error.c_str());

  ImGui::Spacing();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    hide();
    return;
  }

  ImGui::SameLine();
  if (ImGui::Button(button)) {
    std::filesystem::path path(m_directory.data());
    if (!validate(path, m_error))
      return;
    hide();
    if (m_onAccept)
      m_onAccept(path);
  }
}

} // namespace vsr::scivis_studio
