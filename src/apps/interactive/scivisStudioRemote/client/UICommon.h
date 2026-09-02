// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// imgui
#include <imgui.h>
// std
#include <filesystem>
#include <string>

namespace vsr::scivis_studio::client::ui {

/*
 * Small ImGui helpers the adapted editors share. Nothing here reads the
 * filesystem: the path helpers are lexical and operate on server paths.
 *
 * Example:
 *   ui::errorText(m_nameError);
 *   ops.saveDatasetArchive(id, ui::withVsrExtension(chosen), cb);
 */

constexpr ImVec4 ERROR_TEXT_COLOR{1.f, 0.4f, 0.4f, 1.f};
constexpr ImVec4 WARNING_TEXT_COLOR{1.f, 0.75f, 0.3f, 1.f};
constexpr ImVec4 PROJECT_DIRECTORY_COLOR{0.55f, 0.8f, 1.f, 1.f};

// Wrapped red text; nothing when `text` is empty.
void errorText(const std::string &text);
void warningText(const std::string &text);

// Rig and dataset archives default to .vsr when the user typed no extension.
std::filesystem::path withVsrExtension(const std::filesystem::path &file);

// "12.3 MB" style sizes for the browse table.
std::string formatByteCount(uint64_t bytes);
// Local time "YYYY-MM-DD HH:MM"; "" for 0.
std::string formatUnixSeconds(int64_t seconds);

} // namespace vsr::scivis_studio::client::ui
