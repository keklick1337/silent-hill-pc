// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/item_screen_style.h"

#include <stdexcept>

#include "engine/screens/save_layout.h"
#include "engine/screens/screen_data.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using json::Value;
using S = ItemScreenStyle;

struct Bad : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const Value& at, const std::string& what) { throw Bad(at_line(at) + what); }

struct IntKey {
    const char* name;
    int S::*field;
    int lo, hi;
};

constexpr IntKey kKeys[] = {
    {"rule_x0", &S::rule_x0, -1024, 1024}, {"rule_x1", &S::rule_x1, -1024, 1024},
    {"rule_y0", &S::rule_y0, -1024, 1024}, {"rule_y1", &S::rule_y1, -1024, 1024},
    {"notice_x", &S::notice_x, -1024, 1024}, {"notice_y", &S::notice_y, -1024, 1024},
};

}  // namespace

Result<ItemScreenStyle> parse_item_screen_style(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<ItemScreenStyle>::fail(doc.error());
    }
    const Value& root = doc.value();
    if (!root.is_object()) {
        return Result<ItemScreenStyle>::fail("item screen style must be an object");
    }
    ItemScreenStyle s;
    for (const auto& [key, v] : root.members()) {
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (key == "format") {
            if (!v.is_string() || v.string() != "sh-item-screens") {
                fail(v, "\"format\" must be \"sh-item-screens\"");
            }
            continue;
        }
        if (key == "version") {
            if (!v.is_integer() || v.integer() != 1) {
                fail(v, "\"version\" must be 1");
            }
            continue;
        }
        if (key == "rule_color") {
            if (!v.is_array() || v.array().size() != 3) {
                fail(v, "\"rule_color\" must be [r, g, b]");
            }
            for (std::size_t i = 0; i < 3; ++i) {
                const Value& c = v.array()[i];
                if (!c.is_integer() || c.integer() < 0 || c.integer() > 255) {
                    fail(c, "\"rule_color\" channels must be integers in 0..255");
                }
                s.rule_color[i] = static_cast<int>(c.integer());
            }
            continue;
        }
        if (key == "notice_color") {
            const int slot = v.is_string() ? menu_text_color(v.string()) : -1;
            if (slot < 0) {
                fail(v, "\"notice_color\" must be a menu font colour name");
            }
            s.notice_color = slot;
            continue;
        }
        if (key == "notice_too_dark" || key == "notice_no_map") {
            if (!v.is_string() || v.string().empty()) {
                fail(v, "\"" + key + "\" must be a non-empty string");
            }
            (key == "notice_too_dark" ? s.notice_too_dark : s.notice_no_map) = v.string();
            continue;
        }
        const IntKey* found = nullptr;
        for (const IntKey& k : kKeys) {
            if (key == k.name) {
                found = &k;
            }
        }
        if (found == nullptr) {
            fail(v, "unknown key \"" + key + "\"");
        }
        if (!v.is_integer() || v.integer() < found->lo || v.integer() > found->hi) {
            fail(v, "\"" + key + "\" must be an integer in " + std::to_string(found->lo) + ".." +
                        std::to_string(found->hi));
        }
        s.*(found->field) = static_cast<int>(v.integer());
    }
    return Result<ItemScreenStyle>::ok(std::move(s));
} catch (const std::exception& e) {
    return Result<ItemScreenStyle>::fail(e.what());
}

Result<ItemScreenStyle> load_item_screen_style(const std::filesystem::path& gamedata_dir,
                                               const std::filesystem::path& mods_dir, std::string* source) {
    std::string where;
    const Result<std::string> text =
        read_screen_file(gamedata_dir, mods_dir, std::filesystem::path("menu") / "item_screens.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return Result<ItemScreenStyle>::fail(text.error());
    }
    Result<ItemScreenStyle> parsed = parse_item_screen_style(text.value());
    if (!parsed) {
        return Result<ItemScreenStyle>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
