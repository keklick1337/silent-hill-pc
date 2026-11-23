// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/desktop/desktop_entry.h"

#include "engine/desktop/desktop_entry_template.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace sh::desktop {

namespace fs = std::filesystem;

namespace {

fs::path entry_path(const fs::path& data_home) {
    return data_home / "applications" / (std::string(kAppId) + ".desktop");
}

fs::path icon_path(const fs::path& data_home, int size) {
    const std::string dir = std::to_string(size) + "x" + std::to_string(size);
    return data_home / "icons" / "hicolor" / dir / "apps" / (std::string(kAppId) + ".png");
}

void replace_all(std::string& text, std::string_view what, std::string_view with) {
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) {
        text.replace(at, what.size(), with);
    }
}

}  // namespace

fs::path user_data_home() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0] == '/') {
        return fs::path(xdg);
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return fs::path(home) / ".local" / "share";
    }
    return {};
}

std::string string_value(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

std::string exec_value(std::string_view program) {
    // Inside the quotes the four characters the shell-like rules reserve take a backslash; the result is then a
    // string value (every backslash doubled), and a literal percent sign is doubled against the field codes.
    std::string quoted = "\"";
    for (const char c : program) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    quoted += '"';
    std::string out = string_value(quoted);
    replace_all(out, "%", "%%");
    return out;
}

std::string entry_text(const fs::path& exe) {
    std::string text = kEntryTemplate;
    replace_all(text, "@EXEC@", exec_value(exe.string()));
    replace_all(text, "@TRYEXEC@", string_value(exe.string()));
    replace_all(text, "@PATH@", string_value(exe.parent_path().string()));
    return text;
}

Result<std::vector<fs::path>> install(const fs::path& exe, const fs::path& icon_dir, const fs::path& data_home) {
    using R = Result<std::vector<fs::path>>;
    if (data_home.empty()) {
        return R::fail("neither XDG_DATA_HOME nor HOME is set");
    }
    if (!exe.is_absolute()) {
        return R::fail("the path of the executable is not absolute: " + exe.string());
    }
    std::vector<fs::path> written;
    std::error_code ec;

    const fs::path entry = entry_path(data_home);
    fs::create_directories(entry.parent_path(), ec);
    {
        std::ofstream out(entry, std::ios::binary | std::ios::trunc);
        out << entry_text(exe);
        if (!out.flush()) {
            return R::fail("cannot write " + entry.string());
        }
    }
    written.push_back(entry);

    for (const int size : kIconSizes) {
        const fs::path from = icon_dir / ("app_" + std::to_string(size) + ".png");
        if (!fs::is_regular_file(from, ec)) {
            continue;
        }
        const fs::path to = icon_path(data_home, size);
        fs::create_directories(to.parent_path(), ec);
        if (!fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec)) {
            return R::fail("cannot write " + to.string() + ": " + ec.message());
        }
        written.push_back(to);
    }
    // Icon caches look at the time of the theme directory.
    fs::last_write_time(data_home / "icons" / "hicolor", fs::file_time_type::clock::now(), ec);
    return R::ok(std::move(written));
}

Result<std::vector<fs::path>> uninstall(const fs::path& data_home) {
    using R = Result<std::vector<fs::path>>;
    if (data_home.empty()) {
        return R::fail("neither XDG_DATA_HOME nor HOME is set");
    }
    std::vector<fs::path> files = {entry_path(data_home)};
    for (const int size : kIconSizes) {
        files.push_back(icon_path(data_home, size));
    }
    std::vector<fs::path> removed;
    for (const fs::path& file : files) {
        std::error_code ec;
        if (fs::remove(file, ec)) {
            removed.push_back(file);
        } else if (ec) {
            return R::fail("cannot remove " + file.string() + ": " + ec.message());
        }
    }
    return R::ok(std::move(removed));
}

}  // namespace sh::desktop
