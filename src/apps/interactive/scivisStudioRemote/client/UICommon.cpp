// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "UICommon.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// std
#include <cstdio>
#include <ctime>

namespace vsr::scivis_studio::client::ui {

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

std::filesystem::path withVsrExtension(const std::filesystem::path &file)
{
  std::filesystem::path result = file;
  if (result.extension().empty())
    result.replace_extension(ARCHIVE_EXTENSIONS.front());
  return result;
}

// Name field /////////////////////////////////////////////////////////////////

std::optional<std::string> BufferedNameField::draw(const std::string &entityId,
    const std::string &name,
    bool renamePending,
    const char *errorPrefix)
{
  const bool switched = m_entityId != entityId;
  const bool refresh = m_stale && !ImGui::IsAnyItemActive() && !renamePending;
  if (switched || refresh) {
    if (switched)
      m_error.clear(); // the last entity's refusal is not this one's
    m_entityId = entityId;
    m_buffer = name;
    m_stale = false;
  }

  ImGui::BeginDisabled(renamePending);
  const bool entered =
      ImGui::InputText("Name", &m_buffer, ImGuiInputTextFlags_EnterReturnsTrue);
  const bool commit = entered || ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();

  std::optional<std::string> committed;
  if (commit && m_buffer != name)
    committed = m_buffer;
  else if (commit)
    m_error.clear();
  if (!m_error.empty())
    errorText(errorPrefix + m_error);
  return committed;
}

void BufferedNameField::onReply(
    const std::string &entityId, bool ok, const std::string &error)
{
  if (ok) {
    m_error.clear();
    return;
  }
  m_error = error;
  if (m_entityId == entityId)
    m_stale = true; // rejected: back to the replica's name
}

void BufferedNameField::markStale()
{
  m_stale = true;
}

// Confirmation modal /////////////////////////////////////////////////////////

ConfirmChoice confirmModal(const char *popupId,
    const std::string &message,
    const char *confirmLabel,
    bool confirmEnabled,
    const std::function<void()> &body)
{
  if (!ImGui::BeginPopupModal(
          popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return ConfirmChoice::Pending;

  ImGui::TextWrapped("%s", message.c_str());
  if (body)
    body();

  auto choice = ConfirmChoice::Pending;
  ImGui::BeginDisabled(!confirmEnabled);
  if (ImGui::Button(confirmLabel))
    choice = ConfirmChoice::Confirmed;
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    choice = ConfirmChoice::Cancelled;
  if (choice != ConfirmChoice::Pending)
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return choice;
}

std::string formatByteCount(uint64_t bytes)
{
  constexpr const char *UNITS[] = {"B", "KB", "MB", "GB", "TB"};
  double value = double(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[32];
  if (unit == 0)
    std::snprintf(buffer, sizeof(buffer), "%llu B", (unsigned long long)bytes);
  else
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, UNITS[unit]);
  return buffer;
}

std::string formatUnixSeconds(int64_t seconds)
{
  if (seconds == 0)
    return {};
  const std::time_t t = std::time_t(seconds);
  // std::localtime, as the rest of the tree (Viewport.cpp): the UI thread is
  // the only caller, so the shared buffer is fine and MSVC needs no variant.
  const std::tm *local = std::localtime(&t);
  if (!local)
    return {};
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", local) == 0)
    return {};
  return buffer;
}

} // namespace vsr::scivis_studio::client::ui
