// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/save_layout.h"

#include <stdexcept>

#include "engine/screens/screen_data.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using json::Value;
using L = SaveScreenLayout;

struct IntKey {
    const char* name;
    int L::*field;
    int lo, hi;
};

constexpr IntKey kKeys[] = {
    {"visible_rows", &L::visible_rows, 1, 16},
    {"heading_y", &L::heading_y, -512, 512},
    {"center_x", &L::center_x, -512, 1024},
    {"list_x", &L::list_x, -512, 512},
    {"list_y", &L::list_y, -512, 512},
    {"list_w", &L::list_w, 1, 1024},
    {"list_extra_h", &L::list_extra_h, -512, 512},
    {"row_y", &L::row_y, -512, 512},
    {"row_pitch", &L::row_pitch, 1, 256},
    {"row_hit_dy", &L::row_hit_dy, -256, 256},
    {"empty_row", &L::empty_row, 0, 16},
    {"row_text_x", &L::row_text_x, -512, 512},
    {"row_text_right", &L::row_text_right, -512, 1024},
    {"select_inset_x", &L::select_inset_x, -256, 256},
    {"select_inset_y", &L::select_inset_y, -256, 256},
    {"select_glow", &L::select_glow, 0, 255},
    {"select_glow_idle", &L::select_glow_idle, 0, 255},
    {"arrow_x", &L::arrow_x, -512, 1024},
    {"arrow_up_dy", &L::arrow_up_dy, -256, 256},
    {"arrow_down_dy", &L::arrow_down_dy, -256, 256},
    {"hint_y", &L::hint_y, -512, 512},
    {"info_y", &L::info_y, -512, 512},
    {"info_h", &L::info_h, 1, 512},
    {"info_line1", &L::info_line1, -512, 512},
    {"info_line2", &L::info_line2, -512, 512},
    {"yes_cx", &L::yes_cx, -512, 1024},
    {"no_cx", &L::no_cx, -512, 1024},
    {"answer_hit_half_w", &L::answer_hit_half_w, 0, 512},
    {"answer_hit_top", &L::answer_hit_top, -256, 256},
    {"answer_hit_bottom", &L::answer_hit_bottom, -256, 256},
    {"answer_frame_w", &L::answer_frame_w, 1, 512},
    {"answer_frame_h", &L::answer_frame_h, 1, 512},
    {"answer_frame_dy", &L::answer_frame_dy, -256, 256},
    {"answer_frame_glow", &L::answer_frame_glow, 0, 255},
    {"info_label_x", &L::info_label_x, -512, 1024},
    {"info_value_x", &L::info_value_x, -512, 1024},
    {"info_time_x", &L::info_time_x, -512, 1024},
    {"number_digits", &L::number_digits, 1, 9},
    {"save_count_max", &L::save_count_max, 0, 999999},
    {"message_ticks", &L::message_ticks, 1, 100000},
    {"refresh_ticks", &L::refresh_ticks, 1, 100000},
};

struct ColorKey {
    const char* name;
    int SaveScreenColors::*field;
};

constexpr ColorKey kColorKeys[] = {
    {"heading", &SaveScreenColors::heading},
    {"message", &SaveScreenColors::message},
    {"hint", &SaveScreenColors::hint},
    {"no_saves", &SaveScreenColors::no_saves},
    {"new_save", &SaveScreenColors::new_save},
    {"row", &SaveScreenColors::row},
    {"row_next_fear", &SaveScreenColors::row_next_fear},
    {"damaged", &SaveScreenColors::damaged},
    {"difficulty", &SaveScreenColors::difficulty},
    {"question", &SaveScreenColors::question},
    {"answer", &SaveScreenColors::answer},
    {"info_label", &SaveScreenColors::info_label},
    {"info_value", &SaveScreenColors::info_value},
    {"date", &SaveScreenColors::date},
};

struct TextKey {
    const char* name;
    std::string SaveScreenTexts::*field;
};

const TextKey kTextKeys[] = {
    {"heading_save", &SaveScreenTexts::heading_save},
    {"heading_load", &SaveScreenTexts::heading_load},
    {"no_saves", &SaveScreenTexts::no_saves},
    {"new_save", &SaveScreenTexts::new_save},
    {"damaged", &SaveScreenTexts::damaged},
    {"difficulty_easy", &SaveScreenTexts::difficulty_easy},
    {"difficulty_normal", &SaveScreenTexts::difficulty_normal},
    {"difficulty_hard", &SaveScreenTexts::difficulty_hard},
    {"confirm_save", &SaveScreenTexts::confirm_save},
    {"confirm_overwrite", &SaveScreenTexts::confirm_overwrite},
    {"confirm_delete", &SaveScreenTexts::confirm_delete},
    {"yes", &SaveScreenTexts::yes},
    {"no", &SaveScreenTexts::no},
    {"loading", &SaveScreenTexts::loading},
    {"load_failed", &SaveScreenTexts::load_failed},
    {"deleted", &SaveScreenTexts::deleted},
    {"delete_failed", &SaveScreenTexts::delete_failed},
    {"saved", &SaveScreenTexts::saved},
    {"save_failed", &SaveScreenTexts::save_failed},
    {"new_save_info", &SaveScreenTexts::new_save_info},
    {"data", &SaveScreenTexts::data},
    {"time", &SaveScreenTexts::time},
    {"save_count", &SaveScreenTexts::save_count},
    {"delete_hint", &SaveScreenTexts::delete_hint},
};

template <class K, std::size_t N>
const K* find_key(const K (&keys)[N], const std::string& name) {
    for (const K& k : keys) {
        if (name == k.name) {
            return &k;
        }
    }
    return nullptr;
}

std::string parse_colors(const Value& obj, SaveScreenColors& out) {
    if (!obj.is_object()) {
        return at_line(obj) + "\"colors\" must be an object";
    }
    for (const auto& [key, value] : obj.members()) {
        if (key.rfind("_", 0) == 0) {
            continue;
        }
        const ColorKey* k = find_key(kColorKeys, key);
        if (k == nullptr) {
            return at_line(value) + "unknown colour \"" + key + "\"";
        }
        const int slot = value.is_string() ? menu_text_color(value.string()) : -1;
        if (slot < 0) {
            return at_line(value) + "colour \"" + key + "\" must be one of gold, dark_grey, green, red, light_grey, white";
        }
        out.*(k->field) = slot;
    }
    return {};
}

std::string parse_texts(const Value& obj, SaveScreenTexts& out) {
    if (!obj.is_object()) {
        return at_line(obj) + "\"texts\" must be an object";
    }
    for (const auto& [key, value] : obj.members()) {
        if (key.rfind("_", 0) == 0) {
            continue;
        }
        const TextKey* k = find_key(kTextKeys, key);
        if (k == nullptr) {
            return at_line(value) + "unknown text \"" + key + "\"";
        }
        if (!value.is_string() || value.string().empty()) {
            return at_line(value) + "text \"" + key + "\" must be a non-empty text key";
        }
        out.*(k->field) = value.string();
    }
    return {};
}

std::string parse_locations(const Value& arr, std::vector<std::string>& out) {
    if (!arr.is_array() || arr.array().empty()) {
        return at_line(arr) + "\"location_keys\" must be a non-empty array of text keys";
    }
    std::vector<std::string> keys;
    for (const Value& v : arr.array()) {
        if (!v.is_string() || v.string().empty()) {
            return at_line(v) + "\"location_keys\" entries must be non-empty text keys";
        }
        keys.push_back(v.string());
    }
    out = std::move(keys);
    return {};
}

}  // namespace

int menu_text_color(std::string_view name) {
    for (const MenuTextColorName& c : kMenuTextColors) {
        if (name == c.name) {
            return c.slot;
        }
    }
    return -1;
}

Result<SaveScreenLayout> parse_save_layout(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<SaveScreenLayout>::fail(doc.error());
    }
    const Value& root = doc.value();
    if (!root.is_object()) {
        return Result<SaveScreenLayout>::fail("save screen layout must be an object");
    }
    SaveScreenLayout l;
    for (const auto& [key, value] : root.members()) {
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        std::string error;
        if (key == "colors") {
            error = parse_colors(value, l.colors);
        } else if (key == "texts") {
            error = parse_texts(value, l.texts);
        } else if (key == "location_keys") {
            error = parse_locations(value, l.location_keys);
        } else if (const IntKey* found = find_key(kKeys, key)) {
            if (!value.is_integer() || value.integer() < found->lo || value.integer() > found->hi) {
                error = at_line(value) + "\"" + key + "\" must be an integer in " + std::to_string(found->lo) + ".." +
                        std::to_string(found->hi);
            } else {
                l.*(found->field) = static_cast<int>(value.integer());
            }
        } else {
            error = at_line(value) + "unknown key \"" + key + "\"";
        }
        if (!error.empty()) {
            return Result<SaveScreenLayout>::fail(error);
        }
    }
    return Result<SaveScreenLayout>::ok(l);
} catch (const std::exception& e) {
    return Result<SaveScreenLayout>::fail(e.what());
}

Result<SaveScreenLayout> load_save_layout(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("menu") / "save_screen.json";
    std::string file;
    const Result<std::string> text = read_screen_file(gamedata_dir, mods_dir, relative, &file);
    if (source != nullptr) {
        *source = file;
    }
    if (!text) {
        return Result<SaveScreenLayout>::fail(text.error());
    }
    Result<SaveScreenLayout> parsed = parse_save_layout(text.value());
    if (!parsed) {
        return Result<SaveScreenLayout>::fail(file + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
