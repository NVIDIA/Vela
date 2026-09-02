// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// imgui
#include <imgui.h>
// std
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client::ui {

/*
 * Small ImGui helpers the adapted editors share: colours, the buffered name
 * field and the remove-confirmation modal every editor has, and the archive
 * path conventions. Nothing here reads the filesystem: the path helpers are
 * lexical and operate on server paths.
 *
 * Example:
 *   if (auto name = m_nameField.draw(rig.id, rig.name, pending(m_rename)))
 *     m_rename = ops().renameLightRig(rig.id, *name, ...);
 *   ops.saveDatasetArchive(id, ui::withVsrExtension(chosen), cb);
 */

constexpr ImVec4 ERROR_TEXT_COLOR{1.f, 0.4f, 0.4f, 1.f};
constexpr ImVec4 WARNING_TEXT_COLOR{1.f, 0.75f, 0.3f, 1.f};
constexpr ImVec4 PROJECT_DIRECTORY_COLOR{0.55f, 0.8f, 1.f, 1.f};

// Wrapped red text; nothing when `text` is empty.
void errorText(const std::string &text);
void warningText(const std::string &text);

// The extensions rig and dataset archives carry (.tsd is the legacy one);
// Remote Browse greys everything else when asked for an archive.
constexpr std::array<const char *, 2> ARCHIVE_EXTENSIONS = {".vsr", ".tsd"};
// ARCHIVE_EXTENSIONS as a BrowseRequest wants them.
std::vector<std::string> archiveExtensions();
// Rig and dataset archives default to .vsr when the user typed no extension.
std::filesystem::path withVsrExtension(const std::filesystem::path &file);

/*
 * Buffered, reject-on-commit name field: the user edits a copy, a commit
 * (Enter or leaving the field) with a changed value is the caller's cue to
 * send the rename, and the reply either clears the error or shows it and
 * snaps the buffer back to the replica's name. The buffer is refilled when
 * the entity changes and, after markStale(), at the next frame where nothing
 * is being edited and no rename is in flight, so a snapshot never yanks a
 * half-typed name away.
 */
struct BufferedNameField
{
  // Draws the field for `entityId`, whose replica name is `name`; greyed
  // while `renamePending`. Returns the committed name when it differs from
  // `name`. `errorPrefix` heads the error text ("Invalid name: ").
  std::optional<std::string> draw(const std::string &entityId,
      const std::string &name,
      bool renamePending,
      const char *errorPrefix = "");
  // The rename reply for `entityId`.
  void onReply(const std::string &entityId, bool ok, const std::string &error);
  // The replica changed: re-read the name when the user lets go.
  void markStale();

 private:
  std::string m_entityId;
  std::string m_buffer;
  std::string m_error;
  bool m_stale{false};
};

enum class ConfirmChoice
{
  Pending,
  Confirmed,
  Cancelled
};

// The remove/delete confirmation every editor has: a modal `popupId` (the
// caller opened it with ImGui::OpenPopup at window scope) showing `message`,
// then `body` (extra controls, optional), then the confirm button labelled
// `confirmLabel` (greyed unless `confirmEnabled`) beside Cancel; Escape
// cancels. Closes itself on either choice.
ConfirmChoice confirmModal(const char *popupId,
    const std::string &message,
    const char *confirmLabel,
    bool confirmEnabled,
    const std::function<void()> &body = {});

// "12.3 MB" style sizes for the browse table.
std::string formatByteCount(uint64_t bytes);
// Local time "YYYY-MM-DD HH:MM"; "" for 0.
std::string formatUnixSeconds(int64_t seconds);

} // namespace vsr::scivis_studio::client::ui
