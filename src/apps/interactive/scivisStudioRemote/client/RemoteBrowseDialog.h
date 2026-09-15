// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorContext.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
// vsr_scivis_studio_modals
#include "modals/BrowseProvider.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

// The browse vocabulary is the shared modals' (BrowseProvider.h); Remote
// Browse is one provider of it, the monolith's native SDL dialogs are the
// other.
using modals::BrowseMode;
using modals::BrowseRequest;

/*
 * Remote Browse: the one dumb dialog that replaces every native file dialog.
 * It walks the server's Data Roots with ListRoots/ListDirectory, shows the
 * entries (name, kind, size, modified) with project directories marked, and
 * hands back an absolute server path. The free-text path field is the
 * power-user escape hatch: whatever is typed is accepted as is. Hints --
 * greyed non-matching files, the overwrite warning -- are advisory; the op
 * that consumes the path is the authority, and its error comes back on the
 * reply. Nothing here touches the local filesystem; path handling is lexical.
 *
 * Replies arrive on later frames, so the dialog polls its pending request
 * handles. Each owner keeps its own instance and renders it from its own
 * buildUI (nested in the owner's popup when the owner is a modal).
 *
 * Example:
 *   BrowseRequest request;
 *   request.mode = BrowseMode::OpenFile;
 *   request.title = "Load Dataset Archive";
 *   request.extensions = {".vsr", ".tsd"};
 *   request.onAccept = [&](const auto &paths) { load(paths.front()); };
 *   m_browse.open(std::move(request));
 *   ...
 *   m_browse.renderUI(); // every frame
 */
struct RemoteBrowseDialog : public vsr::ui::imgui::Modal
{
  RemoteBrowseDialog(vsr::ui::imgui::Application *app, EditorContext *context);
  ~RemoteBrowseDialog() override;

  void open(BrowseRequest request);
  // The directory shown last; where the next open() without a start lands.
  const std::filesystem::path &lastDirectory() const;

 private:
  void buildUI() override;
  void buildUI_toolbar();
  void buildUI_breadcrumb();
  void buildUI_entries();
  void buildUI_target();
  void buildUI_buttons();

  void requestRoots();
  void navigateTo(const std::filesystem::path &directory);
  void goUp();
  void selectEntry(int index, bool extendSelection);
  void activateEntry(int index);
  void accept();
  void cancel();

  bool listing() const;
  bool isRoot(const std::filesystem::path &directory) const;
  bool matchesFilter(const protocol::DirectoryEntry &entry) const;
  bool choosable(const protocol::DirectoryEntry &entry) const;
  std::vector<std::filesystem::path> chosenPaths(std::string &error) const;
  const protocol::DirectoryEntry *existingFile(const std::string &name) const;

  // Data /////////////////////////////////////////////////////////////////////

  EditorContext *m_context{nullptr};
  BrowseRequest m_request;

  std::vector<std::filesystem::path> m_roots;
  std::filesystem::path m_directory;
  std::vector<protocol::DirectoryEntry> m_entries;
  std::filesystem::path m_lastDirectory;

  int m_selected{-1};
  std::vector<char> m_multiSelected; // OpenFiles
  std::string m_target; // the path field (OpenFile/OpenFiles/OpenDirectory)
  std::string m_fileName; // SaveFile
  std::string m_error;

  InFlight m_pendingRoots;
  InFlight m_pendingList;
  std::filesystem::path m_requestedDirectory;
};

/*
 * Remote Browse as a shared modal's BrowseProvider: one dialog per owner,
 * nested in the owner's popup, so the owner's Escape has to wait while it is
 * up.
 */
struct RemoteBrowseProvider : public modals::BrowseProvider
{
  RemoteBrowseProvider(
      vsr::ui::imgui::Application *app, EditorContext *context);
  ~RemoteBrowseProvider() override;

  void browse(BrowseRequest request) override;
  void renderUI() override;
  bool visible() const override;

 private:
  RemoteBrowseDialog m_dialog;
};

} // namespace vsr::scivis_studio::client
