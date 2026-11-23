// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/desktop/desktop_entry.h"
#include "formats/png/sh_png.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool has_line(const std::string& text, const std::string& line) {
    return ("\n" + text).find("\n" + line + "\n") != std::string::npos;
}

}  // namespace

SH_TEST(desktop_entry_values_are_escaped) {
    SH_CHECK(sh::desktop::string_value("/opt/a b") == "/opt/a b");
    SH_CHECK(sh::desktop::string_value("a\\b") == "a\\\\b");
    SH_CHECK(sh::desktop::exec_value("/opt/Silent Hill/SilentHillPC") == "\"/opt/Silent Hill/SilentHillPC\"");
    // A dollar sign takes a backslash inside the quotes, and that backslash is doubled as a string value.
    SH_CHECK(sh::desktop::exec_value("/a$b") == "\"/a\\\\$b\"");
    SH_CHECK(sh::desktop::exec_value("/100%/x") == "\"/100%%/x\"");
}

SH_TEST(desktop_entry_names_the_exe_and_the_window_class) {
    const std::string text = sh::desktop::entry_text("/opt/Silent Hill/SilentHillPC");
    SH_CHECK(text.rfind("[Desktop Entry]\n", 0) == 0);
    SH_CHECK(has_line(text, "Exec=\"/opt/Silent Hill/SilentHillPC\""));
    SH_CHECK(has_line(text, "TryExec=/opt/Silent Hill/SilentHillPC"));
    SH_CHECK(has_line(text, "Path=/opt/Silent Hill"));
    SH_CHECK(has_line(text, std::string("Icon=") + sh::desktop::kAppId));
    SH_CHECK(has_line(text, std::string("StartupWMClass=") + sh::desktop::kAppId));
    SH_CHECK(has_line(text, "Categories=Game;AdventureGame;"));
    SH_CHECK(text.find("Comment[ru]=") != std::string::npos);
    SH_CHECK(text.find('@') == std::string::npos);
}

SH_TEST(desktop_install_writes_only_under_the_data_home_and_uninstall_removes_it) {
#if !defined(__linux__)
    SH_SKIP("the applications-menu entry is for Linux desktops");
#endif
    const fs::path work = fs::temp_directory_path() / "sh_desktop_entry_test";
    std::error_code ec;
    fs::remove_all(work, ec);
    const fs::path data_home = work / "home" / ".local" / "share";
    const fs::path icons = fs::path(SH_PROJECT_ROOT) / "assets" / "icon";

    const auto installed = sh::desktop::install("/opt/sh/SilentHillPC", icons, data_home);
    SH_CHECK(static_cast<bool>(installed));
    SH_CHECK(installed.value().size() == 1 + sh::desktop::kIconSizes.size());
    for (const fs::path& file : installed.value()) {
        SH_CHECK(fs::is_regular_file(file));
        SH_CHECK(file.string().rfind(data_home.string(), 0) == 0);
    }
    SH_CHECK(read_text(data_home / "applications" / "silent-hill-pc.desktop") ==
             sh::desktop::entry_text("/opt/sh/SilentHillPC"));
    SH_CHECK(fs::is_regular_file(data_home / "icons/hicolor/48x48/apps/silent-hill-pc.png"));

    SH_CHECK(!sh::desktop::install("SilentHillPC", icons, data_home));
    SH_CHECK(!sh::desktop::install("/opt/sh/SilentHillPC", icons, {}));

    const auto removed = sh::desktop::uninstall(data_home);
    SH_CHECK(static_cast<bool>(removed));
    SH_CHECK(removed.value().size() == installed.value().size());
    SH_CHECK(!fs::exists(data_home / "applications" / "silent-hill-pc.desktop"));
    const auto again = sh::desktop::uninstall(data_home);
    SH_CHECK(static_cast<bool>(again) && again.value().empty());
    fs::remove_all(work, ec);
}

// The committed sizes are what generate.sh makes from the master: square pictures of the size their name says.
SH_TEST(app_icon_sizes_are_square_pictures_of_their_size) {
    const fs::path icons = fs::path(SH_PROJECT_ROOT) / "assets" / "icon";
    for (const int size : sh::desktop::kIconSizes) {
        const std::string bytes = read_text(icons / ("app_" + std::to_string(size) + ".png"));
        SH_CHECK(!bytes.empty());
        const auto picture = sh::png::decode_rgba8(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
        SH_CHECK(static_cast<bool>(picture));
        SH_CHECK(picture.value().width == static_cast<std::uint32_t>(size));
        SH_CHECK(picture.value().height == static_cast<std::uint32_t>(size));
    }
    // The Windows resource: an ICO directory (reserved 0, type 1) with at least the small and the large picture.
    const std::string ico = read_text(icons / "app.ico");
    SH_CHECK(ico.size() > 6 && ico[0] == 0 && ico[1] == 0 && ico[2] == 1 && ico[3] == 0);
    SH_CHECK(static_cast<unsigned char>(ico[4]) >= 4);
}
