// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "UICommon.h"
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

std::filesystem::path withVsrExtension(const std::filesystem::path &file)
{
  std::filesystem::path result = file;
  if (result.extension().empty())
    result.replace_extension(".vsr");
  return result;
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
