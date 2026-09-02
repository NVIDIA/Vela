// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RemoteBrowseDialog.h"
// scivisStudioClient
#include "UICommon.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <cctype>
#include <utility>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

constexpr float ENTRIES_WIDTH = 760.f;
constexpr float ENTRIES_HEIGHT = 340.f;

std::string lowerCase(std::string text)
{
  for (char &c : text)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::filesystem::path normalized(const std::filesystem::path &path)
{
  auto result = path.lexically_normal();
  // lexically_normal keeps a trailing slash on "dir/"; strip it so two
  // spellings of one directory compare equal.
  if (result.has_filename())
    return result;
  auto text = result.generic_string();
  if (text.size() > 1 && text.back() == '/')
    text.pop_back();
  return std::filesystem::path(text);
}

const char *kindText(EntryKind kind)
{
  switch (kind) {
  case EntryKind::File:
    return "";
  case EntryKind::Directory:
    return "dir";
  case EntryKind::ProjectDirectory:
    return "project";
  }
  return "";
}

bool isDirectory(const DirectoryEntry &entry)
{
  return entry.kind != EntryKind::File;
}

const char *modeButtonLabel(BrowseMode mode)
{
  switch (mode) {
  case BrowseMode::OpenFile:
  case BrowseMode::OpenFiles:
    return "Open";
  case BrowseMode::SaveFile:
    return "Save";
  case BrowseMode::OpenDirectory:
    return "Choose";
  }
  return "OK";
}

} // namespace

// Construction ///////////////////////////////////////////////////////////////

RemoteBrowseDialog::RemoteBrowseDialog(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : Modal(app, "Remote Browse"), m_context(context)
{}

RemoteBrowseDialog::~RemoteBrowseDialog() = default;

void RemoteBrowseDialog::open(BrowseRequest request)
{
  m_request = std::move(request);
  m_roots.clear();
  m_entries.clear();
  m_directory.clear();
  m_selected = -1;
  m_multiSelected.clear();
  m_target.clear();
  m_fileName = m_request.defaultName;
  m_error.clear();
  // Stale replies for earlier browses are recognised by handle and dropped.
  m_pendingRoots = {};
  m_pendingList = {};
  m_requestedDirectory.clear();

  show();

  if (!m_context->canSend()) {
    m_error = "not connected";
    return;
  }

  requestRoots();
  const auto &start = !m_request.startDirectory.empty()
      ? m_request.startDirectory
      : m_lastDirectory;
  if (!start.empty())
    navigateTo(start);
}

const std::filesystem::path &RemoteBrowseDialog::lastDirectory() const
{
  return m_lastDirectory;
}

// Requests ///////////////////////////////////////////////////////////////////

void RemoteBrowseDialog::requestRoots()
{
  m_pendingRoots = m_context->ops().listRoots(
      [this](const ProjectOpReply &reply,
          const std::optional<ListRootsResult> &result) {
        if (reply.requestId != m_pendingRoots.requestId)
          return;
        m_pendingRoots = {};
        if (!reply.ok) {
          m_error = reply.error;
          return;
        }
        m_roots = result ? result->roots : std::vector<std::filesystem::path>{};
        // Nowhere to start from yet: land in the first root.
        if (m_directory.empty() && !m_pendingList.valid() && !m_roots.empty())
          navigateTo(m_roots.front());
      });
}

void RemoteBrowseDialog::navigateTo(const std::filesystem::path &directory)
{
  if (directory.empty())
    return;
  m_error.clear();
  m_requestedDirectory = normalized(directory);
  m_pendingList = m_context->ops().listDirectory(m_requestedDirectory,
      [this](const ProjectOpReply &reply,
          const std::optional<ListDirectoryResult> &result) {
        if (reply.requestId != m_pendingList.requestId)
          return;
        m_pendingList = {};
        if (!reply.ok) {
          m_error = reply.error;
          return;
        }
        m_directory = m_requestedDirectory;
        m_lastDirectory = m_directory;
        m_entries = result ? result->entries : std::vector<DirectoryEntry>{};
        m_selected = -1;
        m_multiSelected.assign(m_entries.size(), 0);
        if (m_request.mode == BrowseMode::OpenDirectory)
          m_target = m_directory.generic_string();
        else if (m_request.mode != BrowseMode::SaveFile)
          m_target.clear();
      });
}

void RemoteBrowseDialog::goUp()
{
  if (m_directory.empty() || isRoot(m_directory))
    return;
  const auto parent = m_directory.parent_path();
  if (parent.empty() || parent == m_directory)
    return;
  navigateTo(parent);
}

bool RemoteBrowseDialog::listing() const
{
  return m_pendingList.valid() || m_pendingRoots.valid();
}

bool RemoteBrowseDialog::isRoot(const std::filesystem::path &directory) const
{
  const auto wanted = normalized(directory);
  return std::any_of(m_roots.begin(), m_roots.end(), [&](const auto &root) {
    return normalized(root) == wanted;
  });
}

bool RemoteBrowseDialog::matchesFilter(const DirectoryEntry &entry) const
{
  if (m_request.extensions.empty())
    return true;
  const auto extension =
      lowerCase(std::filesystem::path(entry.name).extension().string());
  return std::find(m_request.extensions.begin(),
             m_request.extensions.end(),
             extension)
      != m_request.extensions.end();
}

// Whether clicking the entry makes it the result (as opposed to only
// navigating into it).
bool RemoteBrowseDialog::choosable(const DirectoryEntry &entry) const
{
  if (m_request.mode == BrowseMode::OpenDirectory)
    return isDirectory(entry);
  return !isDirectory(entry);
}

const DirectoryEntry *RemoteBrowseDialog::existingFile(
    const std::string &name) const
{
  auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto &e) {
    return !isDirectory(e) && e.name == name;
  });
  return it == m_entries.end() ? nullptr : &*it;
}

// Selection //////////////////////////////////////////////////////////////////

void RemoteBrowseDialog::selectEntry(int index, bool extendSelection)
{
  if (index < 0 || index >= int(m_entries.size()))
    return;
  const auto &entry = m_entries[index];
  m_selected = index;

  if (m_request.mode == BrowseMode::OpenFiles) {
    if (m_multiSelected.size() != m_entries.size())
      m_multiSelected.assign(m_entries.size(), 0);
    if (!extendSelection)
      std::fill(m_multiSelected.begin(), m_multiSelected.end(), 0);
    if (!isDirectory(entry))
      m_multiSelected[index] = extendSelection ? !m_multiSelected[index] : 1;
    m_target.clear();
    return;
  }

  if (!choosable(entry))
    return;
  const auto path = (m_directory / entry.name).generic_string();
  if (m_request.mode == BrowseMode::SaveFile)
    m_fileName = entry.name;
  else
    m_target = path;
}

void RemoteBrowseDialog::activateEntry(int index)
{
  if (index < 0 || index >= int(m_entries.size()))
    return;
  const auto &entry = m_entries[index];
  if (isDirectory(entry)) {
    navigateTo(m_directory / entry.name);
    return;
  }
  if (m_request.mode != BrowseMode::OpenDirectory) {
    selectEntry(index, false);
    accept();
  }
}

std::vector<std::filesystem::path> RemoteBrowseDialog::chosenPaths(
    std::string &error) const
{
  std::vector<std::filesystem::path> paths;
  switch (m_request.mode) {
  case BrowseMode::OpenFile:
  case BrowseMode::OpenDirectory:
    if (m_target.empty()) {
      error = "Enter or select a path.";
      return {};
    }
    paths.emplace_back(m_target);
    break;
  case BrowseMode::SaveFile: {
    if (m_fileName.empty()) {
      error = "Enter a file name.";
      return {};
    }
    const std::filesystem::path name(m_fileName);
    paths.push_back(name.is_absolute() ? name : m_directory / name);
    break;
  }
  case BrowseMode::OpenFiles:
    for (size_t i = 0; i < m_entries.size() && i < m_multiSelected.size(); ++i) {
      if (m_multiSelected[i])
        paths.push_back(m_directory / m_entries[i].name);
    }
    if (paths.empty() && !m_target.empty())
      paths.emplace_back(m_target);
    if (paths.empty()) {
      error = "Select one or more files.";
      return {};
    }
    break;
  }
  return paths;
}

void RemoteBrowseDialog::accept()
{
  std::string error;
  auto paths = chosenPaths(error);
  if (paths.empty()) {
    m_error = error;
    return;
  }
  hide();
  auto onAccept = std::move(m_request.onAccept);
  m_request.onAccept = nullptr;
  if (onAccept)
    onAccept(paths);
}

void RemoteBrowseDialog::cancel()
{
  hide();
  m_request.onAccept = nullptr;
}

// UI /////////////////////////////////////////////////////////////////////////

void RemoteBrowseDialog::buildUI()
{
  if (!m_request.title.empty())
    ImGui::TextUnformatted(m_request.title.c_str());

  buildUI_toolbar();
  buildUI_breadcrumb();
  buildUI_entries();
  buildUI_target();
  ui::errorText(m_error);
  buildUI_buttons();
}

void RemoteBrowseDialog::buildUI_toolbar()
{
  ImGui::BeginDisabled(listing());

  std::string preview = "Data Roots";
  if (isRoot(m_directory))
    preview = m_directory.generic_string();
  ImGui::SetNextItemWidth(320.f);
  if (ImGui::BeginCombo("##roots", preview.c_str())) {
    if (m_roots.empty())
      ImGui::TextDisabled("<no roots reported>");
    for (const auto &root : m_roots) {
      const auto text = root.generic_string();
      if (ImGui::Selectable(text.c_str(), normalized(root) == m_directory))
        navigateTo(root);
    }
    ImGui::EndCombo();
  }
  vsr::ui::tooltipForPreviousItem("The server's Data Roots");

  ImGui::SameLine();
  ImGui::BeginDisabled(m_directory.empty() || isRoot(m_directory));
  if (ImGui::Button("Up"))
    goUp();
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Refresh") && !m_directory.empty())
    navigateTo(m_directory);

  ImGui::EndDisabled();

  if (listing()) {
    ImGui::SameLine();
    ImGui::TextDisabled("listing...");
  }
}

void RemoteBrowseDialog::buildUI_breadcrumb()
{
  if (m_directory.empty()) {
    ImGui::TextDisabled("<no directory>");
    return;
  }

  ImGui::BeginDisabled(listing());
  std::filesystem::path prefix;
  int i = 0;
  for (const auto &part : m_directory) {
    prefix /= part;
    if (i++ > 0)
      ImGui::SameLine(0.f, 2.f);
    ImGui::PushID(i);
    const auto label = part.generic_string();
    if (ImGui::SmallButton(label.empty() ? "/" : label.c_str()))
      navigateTo(prefix);
    ImGui::PopID();
  }
  ImGui::EndDisabled();
}

void RemoteBrowseDialog::buildUI_entries()
{
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable(
          "entries", 4, flags, ImVec2(ENTRIES_WIDTH, ENTRIES_HEIGHT)))
    return;

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 4.f);
  ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.f);
  ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.f);
  ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthStretch, 2.f);
  ImGui::TableHeadersRow();

  if (m_entries.empty()) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled(
        listing() ? "..." : (m_directory.empty() ? "" : "<empty>"));
  }

  const bool multi = m_request.mode == BrowseMode::OpenFiles;
  for (int i = 0; i < int(m_entries.size()); ++i) {
    const auto &entry = m_entries[i];
    const bool directory = isDirectory(entry);
    const bool dimmed = m_request.mode == BrowseMode::OpenDirectory
        ? !directory
        : (!directory && !matchesFilter(entry));
    const bool selected = multi
        ? (i < int(m_multiSelected.size()) && m_multiSelected[i])
        : i == m_selected;

    ImGui::PushID(i);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    if (entry.kind == EntryKind::ProjectDirectory)
      ImGui::PushStyleColor(ImGuiCol_Text, ui::PROJECT_DIRECTORY_COLOR);
    else if (dimmed)
      ImGui::PushStyleColor(
          ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const std::string label =
        directory ? entry.name + "/" : entry.name;
    if (ImGui::Selectable(label.c_str(),
            selected,
            ImGuiSelectableFlags_SpanAllColumns
                | ImGuiSelectableFlags_AllowDoubleClick)) {
      const bool extend = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        activateEntry(i);
      else
        selectEntry(i, extend);
    }
    if (entry.kind == EntryKind::ProjectDirectory || dimmed)
      ImGui::PopStyleColor();

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(kindText(entry.kind));
    ImGui::TableNextColumn();
    if (!directory)
      ImGui::TextUnformatted(ui::formatByteCount(entry.size).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(ui::formatUnixSeconds(entry.mtimeSeconds).c_str());
    ImGui::PopID();
  }

  ImGui::EndTable();
}

void RemoteBrowseDialog::buildUI_target()
{
  ImGui::SetNextItemWidth(ENTRIES_WIDTH - 90.f);
  if (m_request.mode == BrowseMode::SaveFile) {
    if (ImGui::InputText("File name",
            &m_fileName,
            ImGuiInputTextFlags_EnterReturnsTrue))
      accept();
    if (!m_fileName.empty()
        && !std::filesystem::path(m_fileName).is_absolute()
        && existingFile(m_fileName)) {
      ui::warningText(
          "'" + m_fileName + "' exists here and will be overwritten.");
    }
    return;
  }

  const bool enter = ImGui::InputText(
      "Path", &m_target, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  ImGui::BeginDisabled(m_target.empty() || listing());
  const bool go = ImGui::Button("Go");
  ImGui::EndDisabled();
  vsr::ui::tooltipForPreviousItem("List the typed path as a directory");
  if (go || (enter && m_request.mode == BrowseMode::OpenDirectory))
    navigateTo(m_target);
  else if (enter)
    accept();
}

void RemoteBrowseDialog::buildUI_buttons()
{
  ImGui::Spacing();
  if (ImGui::Button("Cancel")
      || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput)) {
    cancel();
    return;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!m_context->canSend());
  if (ImGui::Button(modeButtonLabel(m_request.mode)))
    accept();
  ImGui::EndDisabled();
}

} // namespace vsr::scivis_studio::client
