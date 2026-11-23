// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/language_set.h"

#include "formats/convert/text/mini_json.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace sh::i18n {
namespace {

namespace fs = std::filesystem;
namespace json = sh::convert::text::json;

constexpr std::string_view kFormat = "sh-text-lang";
constexpr std::string_view kBaseLanguage = "en";

const std::string* string_of(const json::Value& doc, std::string_view key) {
    const json::Value* v = doc.find(key);
    return v != nullptr && v->kind == json::Value::Kind::String ? &v->string : nullptr;
}

}  // namespace

Result<Language> parse_language(std::string_view text, std::string_view code, std::vector<std::string>& warnings) {
    using R = Result<Language>;
    const auto parsed = json::parse(text);
    if (!parsed) {
        return R::fail(parsed.error());
    }
    const json::Value& doc = parsed.value();
    const std::string* format = string_of(doc, "format");
    const json::Value* version = doc.find("version");
    if (doc.kind != json::Value::Kind::Object || format == nullptr || *format != kFormat || version == nullptr ||
        version->kind != json::Value::Kind::Integer || version->integer != 1) {
        return R::fail("not an sh-text-lang v1 document");
    }

    Language lang;
    lang.code = std::string(code);
    const std::string where = "language " + lang.code;
    if (const std::string* own = string_of(doc, "code"); own != nullptr && *own != lang.code) {
        warnings.push_back(where + ": \"code\" is '" + *own + "', the directory name is used");
    }
    for (std::size_t i = 0; i < doc.keys.size(); ++i) {
        const std::string& key = doc.keys[i];
        const json::Value& value = doc.items[i];
        const bool is_string = value.kind == json::Value::Kind::String;
        if (key == "format" || key == "version" || key == "code" || key == "description") {
            continue;
        }
        if (key == "name" && is_string) {
            lang.name = value.string;
        } else if (key == "english_name" && is_string) {
            lang.english_name = value.string;
        } else if (key == "direction" && is_string && (value.string == "ltr" || value.string == "rtl")) {
            lang.direction = value.string == "rtl" ? Direction::Rtl : Direction::Ltr;
        } else if (key == "fonts" && value.kind == json::Value::Kind::Array) {
            for (const json::Value& family : value.items) {
                if (family.kind == json::Value::Kind::String && !family.string.empty()) {
                    lang.fonts.push_back(family.string);
                } else {
                    warnings.push_back(where + ": \"fonts\" holds font family names");
                }
            }
        } else if (key == "glyph_cell_height" && value.kind == json::Value::Kind::Integer) {
            if (value.integer >= 16 && value.integer <= 32) {
                lang.glyph_cell_height = static_cast<int>(value.integer);
            } else {
                warnings.push_back(where + ": \"glyph_cell_height\" is 16..32 rows");
            }
        } else if (key == "menu_order" && value.kind == json::Value::Kind::Integer) {
            if (value.integer >= -1000 && value.integer <= 1000) {
                lang.menu_order = static_cast<int>(value.integer);
            } else {
                warnings.push_back(where + ": \"menu_order\" is -1000..1000");
            }
        } else if (key == "menu" || key == "font") {
            // The older form of the file: "name" is the English name, "menu" the language's own, "font" a glyph
            // set. Read below.
        } else {
            warnings.push_back(where + ": \"" + key + "\" is not a key of lang.json, or has the wrong type");
        }
    }
    if (const std::string* own = string_of(doc, "menu"); own != nullptr && !own->empty()) {
        if (lang.english_name.empty()) {
            lang.english_name = lang.name;
        }
        lang.name = *own;
    }
    if (lang.name.empty()) {
        lang.name = lang.english_name.empty() ? lang.code : lang.english_name;
    }
    if (lang.english_name.empty()) {
        lang.english_name = lang.code;
    }
    return R::ok(std::move(lang));
}

std::vector<Language> discover_languages(std::span<const fs::path> text_dirs, std::vector<std::string>& warnings) {
    std::vector<Language> found;
    for (const fs::path& text_dir : text_dirs) {
        std::error_code ec;
        if (!fs::is_directory(text_dir, ec)) {
            continue;
        }
        std::vector<fs::path> dirs;
        for (const auto& entry : fs::directory_iterator(text_dir, ec)) {
            if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / "lang.json", ec)) {
                dirs.push_back(entry.path());
            }
        }
        std::sort(dirs.begin(), dirs.end());
        for (const fs::path& dir : dirs) {
            const fs::path file = dir / "lang.json";
            std::ifstream in(file, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto lang = parse_language(text, dir.filename().string(), warnings);
            if (!lang) {
                warnings.push_back(file.string() + ": " + lang.error());
                continue;
            }
            lang.value().descriptor = file;
            std::erase_if(found, [&](const Language& other) { return other.code == lang.value().code; });
            found.push_back(std::move(lang.value()));
        }
    }
    std::sort(found.begin(), found.end(), [](const Language& a, const Language& b) {
        if ((a.code == kBaseLanguage) != (b.code == kBaseLanguage)) {
            return a.code == kBaseLanguage;
        }
        if (a.menu_order != b.menu_order) {
            return a.menu_order < b.menu_order;
        }
        return a.code < b.code;
    });
    return found;
}

}  // namespace sh::i18n
