// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/screen_kit/script.h"

#include <cctype>

namespace sh::screen_kit {

std::vector<ScriptCommand> parse_script(std::string_view text) {
    std::vector<ScriptCommand> out;
    const auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    };
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty() || line.front() == '#') continue;
        std::size_t space = 0;
        while (space < line.size() && !std::isspace(static_cast<unsigned char>(line[space]))) ++space;
        out.push_back({std::string(line.substr(0, space)), std::string(trim(line.substr(space)))});
    }
    return out;
}

}  // namespace sh::screen_kit
