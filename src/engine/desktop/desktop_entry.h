// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Linux applications-menu entry of the game, written only when the user asks (--install-desktop) and only
// into the user's own data directory: applications/silent-hill-pc.desktop with the absolute path of the exe, and
// the icon sizes under icons/hicolor/<N>x<N>/apps/. assets/linux/install-desktop.sh does the same from a shell.
#pragma once

#include "formats/common/status.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sh::desktop {

// The application id: the name of the .desktop file and of the icon, the Wayland app id and the X11 window class
// (SDL takes it from the app metadata), and StartupWMClass of the entry, so the window is matched to its entry.
inline constexpr char kAppId[] = "silent-hill-pc";
inline constexpr char kAppName[] = "Silent Hill PC";

// The sizes of assets/icon/app_<N>.png.
inline constexpr std::array<int, 7> kIconSizes = {16, 24, 32, 48, 64, 128, 256};

// $XDG_DATA_HOME, else $HOME/.local/share; empty when neither is set.
std::filesystem::path user_data_home();

// A path as the value of a string key, and as the quoted program of Exec (Desktop Entry Specification).
std::string string_value(std::string_view text);
std::string exec_value(std::string_view program);

// The text of the entry for this exe.
std::string entry_text(const std::filesystem::path& exe);

// Writes the entry and copies the icons of `icon_dir` (app_<N>.png; a missing size is skipped). Returns the files
// written.
Result<std::vector<std::filesystem::path>> install(const std::filesystem::path& exe,
                                                   const std::filesystem::path& icon_dir,
                                                   const std::filesystem::path& data_home);

// Removes what install() writes; files that are not there are not an error. Returns the files removed.
Result<std::vector<std::filesystem::path>> uninstall(const std::filesystem::path& data_home);

}  // namespace sh::desktop
