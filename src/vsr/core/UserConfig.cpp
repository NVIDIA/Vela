// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/UserConfig.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <cstdlib>
#include <mutex>
#include <system_error>

namespace vsr::core {

namespace {

constexpr const char *USER_CONFIG_DIRECTORY_NAME = "vela";
constexpr const char *LEGACY_USER_CONFIG_DIRECTORY_NAME = "vsr";

// The directory that holds per-user config directories, or empty when the
// environment names none.
std::filesystem::path configBaseDirectory()
{
#ifdef _WIN32
  if (const char *appData = std::getenv("APPDATA"); appData && *appData)
    return std::filesystem::path(appData);
#else
#ifdef __linux__
  // The XDG spec says to ignore a relative XDG_CONFIG_HOME.
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    std::filesystem::path path(xdg);
    if (path.is_absolute())
      return path;
  }
#endif
  if (const char *home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config";
#endif
  return {};
}

// Where pre-Vela builds kept per-user state: always beside HOME, whatever
// XDG_CONFIG_HOME says, since those builds never read it.
std::filesystem::path legacyUserConfigDirectory()
{
#ifdef _WIN32
  if (const char *appData = std::getenv("APPDATA"); appData && *appData)
    return std::filesystem::path(appData) / LEGACY_USER_CONFIG_DIRECTORY_NAME;
#else
  if (const char *home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".config"
        / LEGACY_USER_CONFIG_DIRECTORY_NAME;
  }
#endif
  return {};
}

} // namespace

std::filesystem::path userConfigDirectory()
{
  const auto base = configBaseDirectory();
  return base.empty() ? std::filesystem::path(USER_CONFIG_DIRECTORY_NAME)
                      : base / USER_CONFIG_DIRECTORY_NAME;
}

void warnIfOnlyLegacyUserConfigDirectoryExists()
{
  static std::once_flag once;
  std::call_once(once, []() {
    const auto legacy = legacyUserConfigDirectory();
    if (legacy.empty())
      return;

    const auto current = userConfigDirectory();
    std::error_code ec;
    if (!std::filesystem::is_directory(legacy, ec)
        || std::filesystem::exists(current, ec))
      return;

    logWarning(
        "Vela now keeps per-user settings in '%s'; '%s' is no longer read. "
        "Move its contents (color maps, scripts) to keep them.",
        current.string().c_str(),
        legacy.string().c_str());
  });
}

} // namespace vsr::core
