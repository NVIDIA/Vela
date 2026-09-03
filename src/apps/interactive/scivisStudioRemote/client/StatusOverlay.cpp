// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StatusOverlay.h"
// scivisStudioClient
#include "UICommon.h"
// imgui
#include <imgui.h>
// std
#include <utility>

namespace vsr::scivis_studio::client {

namespace {

constexpr double ERROR_TOAST_SECONDS = 8.0;
constexpr double STATUS_TOAST_SECONDS = 4.0;
constexpr size_t MAX_TOASTS = 5;

} // namespace

void StatusOverlay::pushToast(const std::string &text, bool isError)
{
  Toast toast;
  toast.text = text;
  toast.isError = isError;
  toast.expiresAt =
      ImGui::GetTime() + (isError ? ERROR_TOAST_SECONDS : STATUS_TOAST_SECONDS);
  m_toasts.push_back(std::move(toast));
  while (m_toasts.size() > MAX_TOASTS)
    m_toasts.pop_front();
}

LostBannerChoice StatusOverlay::drawLostBanner(
    bool autoRetrying, const std::string &statusText)
{
  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(mainViewport->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, 0.f));
  ImGui::SetNextWindowBgAlpha(0.95f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.75f, 0.12f, 0.1f, 1.f));

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking
      | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
      | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize
      | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

  auto choice = LostBannerChoice::None;
  if (ImGui::Begin("##connectionLostBanner", nullptr, flags)) {
    if (autoRetrying) {
      ImGui::TextUnformatted("Server connection lost -- reconnecting...");
    } else {
      ImGui::TextUnformatted("Server connection lost");
      ImGui::SameLine();
      if (ImGui::SmallButton("Retry"))
        choice = LostBannerChoice::Retry;
      ImGui::SameLine();
      if (ImGui::SmallButton("Disconnect"))
        choice = LostBannerChoice::Disconnect;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", statusText.c_str());
  }
  ImGui::End();

  ImGui::PopStyleColor();
  return choice;
}

void StatusOverlay::drawToasts()
{
  const double now = ImGui::GetTime();
  while (!m_toasts.empty() && m_toasts.front().expiresAt <= now)
    m_toasts.pop_front();
  if (m_toasts.empty())
    return;

  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  const ImVec2 corner(mainViewport->WorkPos.x + mainViewport->WorkSize.x - 12.f,
      mainViewport->WorkPos.y + mainViewport->WorkSize.y - 12.f);
  ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.f, 1.f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
      | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
      | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
  if (ImGui::Begin("##toasts", nullptr, flags)) {
    ImGui::PushTextWrapPos(520.f);
    for (const Toast &toast : m_toasts) {
      if (toast.isError)
        ImGui::TextColored(ui::ERROR_TEXT_COLOR, "%s", toast.text.c_str());
      else
        ImGui::TextUnformatted(toast.text.c_str());
    }
    ImGui::PopTextWrapPos();
  }
  ImGui::End();
}

} // namespace vsr::scivis_studio::client
