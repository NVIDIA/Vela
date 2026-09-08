// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// imgui
#include <imgui.h>
// std
#include <array>
#include <string>
#include <vector>

namespace vsr::scivis_studio::modals {

/*
 * The scraps of ImGui vocabulary the shared modals need: the two message
 * colours and the archive extensions a browse filters on. Nothing here
 * touches the filesystem.
 *
 * Example:
 *   modals::errorText(m_error);
 *   request.extensions = modals::archiveExtensions();
 */

constexpr ImVec4 ERROR_TEXT_COLOR{1.f, 0.4f, 0.4f, 1.f};
constexpr ImVec4 WARNING_TEXT_COLOR{1.f, 0.75f, 0.3f, 1.f};

// Wrapped red/amber text; nothing when `text` is empty.
void errorText(const std::string &text);
void warningText(const std::string &text);

// The extensions rig and dataset archives carry (.tsd is the legacy one);
// a browse greys everything else when asked for an archive.
constexpr std::array<const char *, 2> ARCHIVE_EXTENSIONS = {".vsr", ".tsd"};
// ARCHIVE_EXTENSIONS as a BrowseRequest wants them.
std::vector<std::string> archiveExtensions();

} // namespace vsr::scivis_studio::modals
