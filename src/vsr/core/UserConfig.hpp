// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <filesystem>

namespace vsr::core {

// The User Config Directory: where Vela keeps everything it remembers between
// runs (Application Preferences, each application's UI State, the user's
// color maps and scripts). See docs/adr/0041.
//
//   Linux:   $XDG_CONFIG_HOME/vela, else ~/.config/vela
//   macOS:   ~/.config/vela
//   Windows: %APPDATA%\vela
//
// Falls back to the relative path "vela" when the variables are unset. The
// directory is not created.
std::filesystem::path userConfigDirectory();

// Logs one warning per process if the pre-Vela directory (~/.config/vsr,
// %APPDATA%\vsr) exists and userConfigDirectory() does not, since nothing
// reads the old one any more. Call it once logging reaches the user.
void warnIfOnlyLegacyUserConfigDirectoryExists();

} // namespace vsr::core
