// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AddFileAnimationDatasetDialog.h"
#include "UICommon.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

// "frame_2" before "frame_10": digit runs compare by value.
int naturalCompareString(const std::string &a, const std::string &b)
{
  size_t ia = 0;
  size_t ib = 0;
  while (ia < a.size() && ib < b.size()) {
    const bool digitA = std::isdigit(static_cast<unsigned char>(a[ia]));
    const bool digitB = std::isdigit(static_cast<unsigned char>(b[ib]));
    if (digitA && digitB) {
      size_t enda = ia;
      size_t endb = ib;
      while (enda < a.size() && std::isdigit(static_cast<unsigned char>(a[enda])))
        ++enda;
      while (endb < b.size() && std::isdigit(static_cast<unsigned char>(b[endb])))
        ++endb;

      auto na = a.substr(ia, enda - ia);
      auto nb = b.substr(ib, endb - ib);
      na.erase(0, std::min(na.find_first_not_of('0'), na.size()));
      nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size()));
      if (na.size() != nb.size())
        return na.size() < nb.size() ? -1 : 1;
      if (na != nb)
        return na < nb ? -1 : 1;
      ia = enda;
      ib = endb;
      continue;
    }

    if (a[ia] != b[ib])
      return a[ia] < b[ib] ? -1 : 1;
    ++ia;
    ++ib;
  }

  if (ia == a.size() && ib == b.size())
    return 0;
  return ia == a.size() ? -1 : 1;
}

bool naturalPathLess(const std::string &a, const std::string &b)
{
  const auto fa = std::filesystem::path(a).filename().string();
  const auto fb = std::filesystem::path(b).filename().string();
  const int filenameCompare = naturalCompareString(fa, fb);
  if (filenameCompare != 0)
    return filenameCompare < 0;
  return naturalCompareString(a, b) < 0;
}

std::string trimmedGeneratedName(std::string name)
{
  while (!name.empty()) {
    const char c = name.back();
    if (c == ' ' || c == '_' || c == '-' || c == '.')
      name.pop_back();
    else
      break;
  }
  return name;
}

std::string commonStemPrefix(const std::vector<std::string> &paths)
{
  if (paths.empty())
    return {};

  std::string prefix = std::filesystem::path(paths.front()).stem().string();
  for (size_t i = 1; i < paths.size() && !prefix.empty(); ++i) {
    const auto stem = std::filesystem::path(paths[i]).stem().string();
    size_t n = 0;
    while (n < prefix.size() && n < stem.size() && prefix[n] == stem[n])
      ++n;
    prefix.resize(n);
  }

  prefix = trimmedGeneratedName(prefix);
  if (!prefix.empty())
    return prefix;
  return std::filesystem::path(paths.front()).stem().string();
}

bool anySelected(const std::vector<char> &selectedRows)
{
  return std::any_of(selectedRows.begin(), selectedRows.end(), [](char s) {
    return s != 0;
  });
}

} // namespace

AddFileAnimationDatasetDialog::AddFileAnimationDatasetDialog(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : Modal(app, "Add File Animation Dataset"),
      m_context(context),
      m_browse(app, context)
{}

AddFileAnimationDatasetDialog::~AddFileAnimationDatasetDialog() = default;

void AddFileAnimationDatasetDialog::reset()
{
  m_name.clear();
  m_nameEditedByUser = false;
  m_sourcePaths.clear();
  m_selectedRows.clear();
  m_extensionWarning.clear();
  m_error.clear();
  m_pending = {};
}

void AddFileAnimationDatasetDialog::updateGeneratedName()
{
  if (!m_nameEditedByUser)
    m_name = commonStemPrefix(m_sourcePaths);
}

void AddFileAnimationDatasetDialog::updateExtensionWarning()
{
  m_extensionWarning.clear();
  std::set<std::string> extensions;
  for (const auto &path : m_sourcePaths)
    extensions.insert(std::filesystem::path(path).extension().string());
  if (extensions.size() <= 1)
    return;
  m_extensionWarning = "Mixed extensions: ";
  bool first = true;
  for (const auto &extension : extensions) {
    if (!first)
      m_extensionWarning += ", ";
    m_extensionWarning += extension.empty() ? "<none>" : extension;
    first = false;
  }
}

void AddFileAnimationDatasetDialog::submit()
{
  if (m_sourcePaths.empty()) {
    m_error = "Select at least one frame.";
    return;
  }
  std::vector<std::filesystem::path> sourcePaths;
  sourcePaths.reserve(m_sourcePaths.size());
  for (const auto &path : m_sourcePaths)
    sourcePaths.emplace_back(path);

  m_error.clear();
  m_pending = m_context->ops().importFileAnimationDataset(m_name,
      sourcePaths,
      vsr::io::ImporterType::VOLUME_ANIMATION,
      true,
      [this](const ProjectOpReply &reply,
          const std::optional<TaskStartedResult> &) {
        if (reply.requestId != m_pending.requestId)
          return;
        m_pending = {};
        if (!reply.ok) {
          m_error = reply.error;
          return;
        }
        reset();
        hide();
      });
}

void AddFileAnimationDatasetDialog::buildUI()
{
  const bool busy = m_pending.valid() && m_context->ops().pending(m_pending);

  ImGui::BeginDisabled(busy);
  ImGui::SetNextItemWidth(420.f);
  if (ImGui::InputText("Name", &m_name))
    m_nameEditedByUser = true;

  buildUI_listControls();

  ImGui::Text("Frames: %zu", m_sourcePaths.size());
  ui::warningText(m_extensionWarning);
  buildUI_frameList();
  ImGui::EndDisabled();

  if (busy)
    ImGui::TextDisabled("waiting for the server...");
  ui::errorText(m_error);

  m_browse.renderUI();

  ImGui::Spacing();
  if (ImGui::Button("Cancel")
      || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_browse.visible())) {
    reset();
    hide();
    return;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(busy || !m_context->canSend());
  if (ImGui::Button("Import"))
    submit();
  ImGui::EndDisabled();
}

void AddFileAnimationDatasetDialog::buildUI_listControls()
{
  auto listChanged = [this] {
    m_selectedRows.resize(m_sourcePaths.size(), 0);
    updateExtensionWarning();
    updateGeneratedName();
  };

  if (ImGui::Button("Add Files...")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenFiles;
    request.title = "Choose the frame files (Ctrl/Shift-click for several)";
    request.onAccept = [this, listChanged](
                           const std::vector<std::filesystem::path> &paths) {
      std::vector<std::string> added;
      for (const auto &path : paths)
        added.push_back(path.generic_string());
      std::sort(added.begin(), added.end(), naturalPathLess);
      m_sourcePaths.insert(m_sourcePaths.end(), added.begin(), added.end());
      listChanged();
    };
    m_browse.open(std::move(request));
  }
  ImGui::SameLine();
  if (ImGui::Button("Remove") && anySelected(m_selectedRows)) {
    for (int i = int(m_sourcePaths.size()) - 1; i >= 0; --i) {
      if (i < int(m_selectedRows.size()) && m_selectedRows[i])
        m_sourcePaths.erase(m_sourcePaths.begin() + i);
    }
    m_selectedRows.assign(m_sourcePaths.size(), 0);
    listChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    m_sourcePaths.clear();
    m_selectedRows.clear();
    listChanged();
  }

  if (ImGui::Button("Move Up") && anySelected(m_selectedRows)
      && !m_selectedRows.front()) {
    for (size_t i = 1; i < m_sourcePaths.size(); ++i) {
      if (m_selectedRows[i] && !m_selectedRows[i - 1]) {
        std::swap(m_sourcePaths[i], m_sourcePaths[i - 1]);
        std::swap(m_selectedRows[i], m_selectedRows[i - 1]);
      }
    }
    listChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button("Move Down") && anySelected(m_selectedRows)
      && !m_selectedRows.back()) {
    for (int i = int(m_sourcePaths.size()) - 2; i >= 0; --i) {
      if (m_selectedRows[i] && !m_selectedRows[i + 1]) {
        std::swap(m_sourcePaths[i], m_sourcePaths[i + 1]);
        std::swap(m_selectedRows[i], m_selectedRows[i + 1]);
      }
    }
    listChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button("Sort by Name")) {
    std::sort(m_sourcePaths.begin(), m_sourcePaths.end(), naturalPathLess);
    m_selectedRows.assign(m_sourcePaths.size(), 0);
    listChanged();
  }
}

void AddFileAnimationDatasetDialog::buildUI_frameList()
{
  if (ImGui::BeginChild("FileAnimationFrames",
          ImVec2(560.f, 240.f),
          ImGuiChildFlags_Borders)) {
    m_selectedRows.resize(m_sourcePaths.size(), 0);
    for (int i = 0; i < int(m_sourcePaths.size()); ++i) {
      const auto filename =
          std::filesystem::path(m_sourcePaths[i]).filename().string();
      const auto label = std::to_string(i) + "  " + filename;
      if (ImGui::Selectable(label.c_str(), m_selectedRows[i] != 0)) {
        const bool append = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
        if (!append)
          m_selectedRows.assign(m_sourcePaths.size(), 0);
        m_selectedRows[i] = m_selectedRows[i] ? 0 : 1;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", m_sourcePaths[i].c_str());
    }
  }
  ImGui::EndChild();
}

} // namespace vsr::scivis_studio::client
