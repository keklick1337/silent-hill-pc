// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/quick_setup/setup_text.h"

#include "formats/common/json.h"

#include <fstream>
#include <iterator>

namespace sh::quick_setup {

namespace {

constexpr const char* kFiles[] = {"setup.json", "options_menu.json"};

}  // namespace

void Strings::load(const std::filesystem::path& text_root, std::string_view code) {
    strings_.clear();
    for (const char* file : kFiles) read(text_root / "en" / file);
    if (code.empty() || code == "en" || code.find_first_of("/\\.") != std::string_view::npos) return;
    for (const char* file : kFiles) read(text_root / std::string(code) / file);
}

void Strings::read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto doc = sh::json::parse(text);
    const sh::json::Value* strings = doc ? doc.value().find("strings") : nullptr;
    if (strings == nullptr || !strings->is_object()) return;
    for (const auto& member : strings->members()) {
        if (member.second.is_string()) strings_[member.first] = member.second.string();
    }
}

bool Strings::has(std::string_view key) const { return strings_.find(std::string(key)) != strings_.end(); }

std::string Strings::get(std::string_view key) const {
    const auto it = strings_.find(std::string(key));
    return it != strings_.end() ? it->second : std::string(key);
}

std::string Strings::format(std::string_view key, std::initializer_list<std::string_view> args) const {
    return fill(get(key), args);
}

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> args) {
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '0' &&
            pattern[i + 1] <= '9') {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (n < args.size()) {
                out += *(args.begin() + n);
                i += 2;
                continue;
            }
        }
        out += pattern[i];
    }
    return out;
}

}  // namespace sh::quick_setup
