// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/controls_layout.h"

#include <cctype>
#include <stdexcept>

#include "engine/screens/save_layout.h"
#include "engine/screens/screen_data.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using json::Value;
using L = ControlsScreenLayout;

struct IntKey {
    const char* name;
    int L::*field;
    int lo, hi;
};

constexpr IntKey kKeys[] = {
    {"tabs_y", &L::tabs_y, -512, 512},
    {"tab_hit_pad", &L::tab_hit_pad, 0, 256},
    {"rule1_y", &L::rule1_y, -512, 512},
    {"head_y", &L::head_y, -512, 512},
    {"rule2_y", &L::rule2_y, -512, 512},
    {"arrow_up_y", &L::arrow_up_y, -512, 512},
    {"list_y", &L::list_y, -512, 512},
    {"row_pitch", &L::row_pitch, 1, 256},
    {"visible_rows", &L::visible_rows, 1, 64},
    {"arrow_down_y", &L::arrow_down_y, -512, 512},
    {"rule3_y", &L::rule3_y, -512, 512},
    {"footer_y", &L::footer_y, -512, 512},
    {"confirm_y", &L::confirm_y, -512, 512},
    {"rule_x0", &L::rule_x0, -512, 1024},
    {"rule_x1", &L::rule_x1, -512, 1024},
    {"text_x", &L::text_x, -512, 1024},
    {"value_right", &L::value_right, -512, 1024},
    {"label_w", &L::label_w, 1, 1024},
    {"highlight_dy", &L::highlight_dy, -256, 256},
    {"arrow_x", &L::arrow_x, -512, 1024},
    {"row_h", &L::row_h, 1, 256},
    {"icon_dy", &L::icon_dy, -256, 256},
    {"icon_layer", &L::icon_layer, 0, 15},
    {"icon_suffix_dx", &L::icon_suffix_dx, -256, 256},
    {"icon_text_dx", &L::icon_text_dx, -256, 256},
    {"icon_origin_x", &L::icon_origin_x, -1024, 1024},
    {"icon_origin_y", &L::icon_origin_y, -1024, 1024},
    {"bullet_highlight_left", &L::bullet_highlight_left, -256, 256},
    {"bullet_highlight_right", &L::bullet_highlight_right, -256, 256},
    {"footer_hit_right", &L::footer_hit_right, -256, 256},
    {"cell_hit_pad", &L::cell_hit_pad, -256, 256},
    {"icon_arrow_hit_w", &L::icon_arrow_hit_w, 0, 256},
    {"confirm_hit_dy", &L::confirm_hit_dy, -512, 512},
    {"confirm_hit_x0", &L::confirm_hit_x0, -512, 1024},
    {"confirm_hit_x1", &L::confirm_hit_x1, -512, 1024},
    {"confirm_split_x", &L::confirm_split_x, -512, 1024},
    {"beep_volume", &L::beep_volume, 0, 127},
};

struct ColorKey {
    const char* name;
    int ControlsScreenColors::*field;
};

constexpr ColorKey kColorKeys[] = {
    {"selected", &ControlsScreenColors::selected},
    {"disabled", &ControlsScreenColors::disabled},
    {"duplicate", &ControlsScreenColors::duplicate},
    {"normal", &ControlsScreenColors::normal},
    {"heading", &ControlsScreenColors::heading},
    {"heading_selected", &ControlsScreenColors::heading_selected},
    {"stick", &ControlsScreenColors::stick},
    {"label", &ControlsScreenColors::label},
    {"value", &ControlsScreenColors::value},
    {"other_device", &ControlsScreenColors::other_device},
};

struct TintKey {
    const char* name;
    std::array<int, 3> ControlsGlyphTints::*field;
};

constexpr TintKey kTintKeys[] = {
    {"normal", &ControlsGlyphTints::normal},
    {"disabled", &ControlsGlyphTints::disabled},
    {"duplicate", &ControlsGlyphTints::duplicate},
};

struct TextKey {
    const char* name;
    std::string ControlsScreenTexts::*field;
    bool may_be_empty;
};

const TextKey kTextKeys[] = {
    {"primary", &ControlsScreenTexts::primary, false},
    {"secondary", &ControlsScreenTexts::secondary, false},
    {"gamepad_icons", &ControlsScreenTexts::gamepad_icons, false},
    {"back", &ControlsScreenTexts::back, false},
    {"reset", &ControlsScreenTexts::reset, false},
    {"reset_controls", &ControlsScreenTexts::reset_controls, false},
    {"action_prefix", &ControlsScreenTexts::action_prefix, false},
    {"listening", &ControlsScreenTexts::listening, true},
    {"unbound", &ControlsScreenTexts::unbound, true},
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

// "_" keys and "description" are notes for people.
bool is_comment(const std::string& key) { return key.rfind("_", 0) == 0 || key == "description"; }

std::string parse_colors(const Value& obj, ControlsScreenColors& out) {
    if (!obj.is_object()) {
        return at_line(obj) + "\"colors\" must be an object";
    }
    for (const auto& [key, value] : obj.members()) {
        if (is_comment(key)) {
            continue;
        }
        const ColorKey* k = find_key(kColorKeys, key);
        if (k == nullptr) {
            return at_line(value) + "unknown colour \"" + key + "\"";
        }
        const int slot = value.is_string() ? menu_text_color(value.string()) : -1;
        if (slot < 0) {
            return at_line(value) + "colour \"" + key +
                   "\" must be one of gold, dark_grey, green, red, light_grey, white";
        }
        out.*(k->field) = slot;
    }
    return {};
}

std::string parse_tints(const Value& obj, ControlsGlyphTints& out) {
    if (!obj.is_object()) {
        return at_line(obj) + "\"icon_tints\" must be an object";
    }
    for (const auto& [key, value] : obj.members()) {
        if (is_comment(key)) {
            continue;
        }
        const TintKey* k = find_key(kTintKeys, key);
        if (k == nullptr) {
            return at_line(value) + "unknown icon tint \"" + key + "\"";
        }
        if (!value.is_array() || value.array().size() != 3) {
            return at_line(value) + "icon tint \"" + key + "\" must be [r, g, b]";
        }
        std::array<int, 3> rgb{};
        for (std::size_t i = 0; i < 3; ++i) {
            const Value& c = value.array()[i];
            if (!c.is_integer() || c.integer() < 0 || c.integer() > 255) {
                return at_line(c) + "icon tint \"" + key + "\" channels must be integers in 0..255";
            }
            rgb[i] = static_cast<int>(c.integer());
        }
        out.*(k->field) = rgb;
    }
    return {};
}

std::string parse_texts(const Value& obj, ControlsScreenTexts& out) {
    if (!obj.is_object()) {
        return at_line(obj) + "\"texts\" must be an object";
    }
    for (const auto& [key, value] : obj.members()) {
        if (is_comment(key)) {
            continue;
        }
        if (key == "tabs") {
            if (!value.is_array() || value.array().size() != kControlsTabs) {
                return at_line(value) + "\"tabs\" must be 3 text keys (keyboard, mouse, gamepad)";
            }
            for (std::size_t i = 0; i < kControlsTabs; ++i) {
                const Value& v = value.array()[i];
                if (!v.is_string() || v.string().empty()) {
                    return at_line(v) + "\"tabs\" entries must be non-empty text keys";
                }
                out.tabs[i] = v.string();
            }
            continue;
        }
        if (key == "stick_rows") {
            if (!value.is_array() || value.array().size() != 4) {
                return at_line(value) + "\"stick_rows\" must be 4 bind names (up, right, down, left)";
            }
            std::array<std::string, 4> rows;
            for (std::size_t i = 0; i < 4; ++i) {
                const Value& v = value.array()[i];
                if (!v.is_string() || v.string().empty()) {
                    return at_line(v) + "\"stick_rows\" entries must be non-empty bind names";
                }
                rows[i] = v.string();
            }
            out.stick_rows = rows;
            continue;
        }
        const TextKey* k = find_key(kTextKeys, key);
        if (k == nullptr) {
            return at_line(value) + "unknown text \"" + key + "\"";
        }
        if (!value.is_string() || (!k->may_be_empty && value.string().empty())) {
            return at_line(value) + "text \"" + key + "\" must be a " + (k->may_be_empty ? "string" : "non-empty text key");
        }
        out.*(k->field) = value.string();
    }
    return {};
}

std::string parse_tab_x(const Value& arr, std::array<int, kControlsTabs>& out) {
    if (!arr.is_array() || arr.array().size() != kControlsTabs) {
        return at_line(arr) + "\"tab_x\" must be 3 centres (keyboard, mouse, gamepad)";
    }
    for (std::size_t i = 0; i < kControlsTabs; ++i) {
        const Value& v = arr.array()[i];
        if (!v.is_integer() || v.integer() < -512 || v.integer() > 1024) {
            return at_line(v) + "\"tab_x\" entries must be integers in -512..1024";
        }
        out[i] = static_cast<int>(v.integer());
    }
    return {};
}

std::string parse_columns(const Value& arr, std::array<ControlsColumn, kControlsSlots>& out) {
    if (!arr.is_array() || arr.array().size() != kControlsSlots) {
        return at_line(arr) + "\"columns\" must be 2 [x, w] pairs (primary, secondary)";
    }
    std::array<ControlsColumn, kControlsSlots> cols{};
    for (std::size_t i = 0; i < kControlsSlots; ++i) {
        const Value& p = arr.array()[i];
        if (!p.is_array() || p.array().size() != 2 || !p.array()[0].is_integer() || !p.array()[1].is_integer() ||
            p.array()[0].integer() < -512 || p.array()[0].integer() > 1024 || p.array()[1].integer() < 1 ||
            p.array()[1].integer() > 1024) {
            return at_line(p) + "\"columns\" entries must be [x -512..1024, w 1..1024]";
        }
        cols[i].x = static_cast<int>(p.array()[0].integer());
        cols[i].w = static_cast<int>(p.array()[1].integer());
    }
    out = cols;
    return {};
}

}  // namespace

Result<ControlsScreenLayout> parse_controls_layout(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<ControlsScreenLayout>::fail(doc.error());
    }
    const Value& root = doc.value();
    if (!root.is_object()) {
        return Result<ControlsScreenLayout>::fail("controls screen layout must be an object");
    }
    ControlsScreenLayout l;
    for (const auto& [key, value] : root.members()) {
        if (is_comment(key)) {
            continue;
        }
        std::string error;
        if (key == "colors") {
            error = parse_colors(value, l.colors);
        } else if (key == "icon_tints") {
            error = parse_tints(value, l.tints);
        } else if (key == "texts") {
            error = parse_texts(value, l.texts);
        } else if (key == "tab_x") {
            error = parse_tab_x(value, l.tab_x);
        } else if (key == "columns") {
            error = parse_columns(value, l.columns);
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
            return Result<ControlsScreenLayout>::fail(error);
        }
    }
    return Result<ControlsScreenLayout>::ok(l);
} catch (const std::exception& e) {
    return Result<ControlsScreenLayout>::fail(e.what());
}

Result<ControlsScreenLayout> load_controls_layout(const std::filesystem::path& gamedata_dir,
                                                  const std::filesystem::path& mods_dir, std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("menu") / "controls_screen.json";
    std::string file;
    const Result<std::string> text = read_screen_file(gamedata_dir, mods_dir, relative, &file);
    if (source != nullptr) {
        *source = file;
    }
    if (!text) {
        return Result<ControlsScreenLayout>::fail(text.error());
    }
    Result<ControlsScreenLayout> parsed = parse_controls_layout(text.value());
    if (!parsed) {
        return Result<ControlsScreenLayout>::fail(file + ": " + parsed.error());
    }
    return parsed;
}

std::vector<int> controls_tab_rows(ControlsTab tab, const std::vector<ControlsRowCaps>& rows) {
    std::vector<int> out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const ControlsRowCaps& r = rows[i];
        const bool listed = tab == ControlsTab::Keyboard ? r.keyboard
                            : tab == ControlsTab::Mouse  ? r.keyboard && r.mouse
                                                         : r.pad || r.stick;
        if (listed) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

ControlsTab controls_tab_step(ControlsTab tab, int dir) {
    const int n = (static_cast<int>(tab) + (dir < 0 ? kControlsTabs - 1 : 1)) % kControlsTabs;
    return static_cast<ControlsTab>(n);
}

int controls_tab_left(const ControlsScreenLayout& l, int tab, int width) {
    return l.tab_x[static_cast<std::size_t>(tab)] - width / 2;
}

int controls_tab_hit(const ControlsScreenLayout& l, const std::array<int, kControlsTabs>& widths, int mx, int my) {
    if (my < l.tabs_y || my >= l.tabs_y + l.row_h) {
        return -1;
    }
    for (int t = 0; t < kControlsTabs; ++t) {
        const int x0 = controls_tab_left(l, t, widths[static_cast<std::size_t>(t)]) - l.tab_hit_pad;
        const int x1 = x0 + widths[static_cast<std::size_t>(t)] + 2 * l.tab_hit_pad;
        if (mx >= x0 && mx < x1) {
            return t;
        }
    }
    return -1;
}

int controls_slot_hit(const ControlsScreenLayout& l, int mx) {
    const ControlsColumn& a = l.columns[0];
    const ControlsColumn& b = l.columns[1];
    if (mx >= a.x - l.cell_hit_pad && mx < b.x - l.cell_hit_pad) {
        return 0;
    }
    if (mx >= b.x - l.cell_hit_pad && mx < b.x + b.w + l.cell_hit_pad) {
        return 1;
    }
    return -1;
}

int controls_list_hit(const ControlsScreenLayout& l, int top, int visible, int count, int my) {
    if (my < l.list_y) {
        return -1;
    }
    const int i = (my - l.list_y) / l.row_pitch;
    if (i >= visible || top + i >= count) {
        return -1;
    }
    return top + i;
}

ControlsCursor controls_move_vertical(ControlsCursor c, int dir, bool has_icons, int count) {
    const bool down = dir > 0;
    switch (c.focus) {
        case ControlsFocus::Tabs:
            if (!down) {
                return {ControlsFocus::Back, 0};
            }
            if (has_icons) {
                return {ControlsFocus::Icons, 0};
            }
            return count > 0 ? ControlsCursor{ControlsFocus::List, 0} : ControlsCursor{ControlsFocus::Back, 0};
        case ControlsFocus::Icons:
            if (!down) {
                return {ControlsFocus::Tabs, 0};
            }
            return count > 0 ? ControlsCursor{ControlsFocus::List, 0} : ControlsCursor{ControlsFocus::Back, 0};
        case ControlsFocus::List:
            if (down) {
                return c.row + 1 < count ? ControlsCursor{ControlsFocus::List, c.row + 1}
                                         : ControlsCursor{ControlsFocus::Back, 0};
            }
            if (c.row > 0) {
                return {ControlsFocus::List, c.row - 1};
            }
            return {has_icons ? ControlsFocus::Icons : ControlsFocus::Tabs, 0};
        case ControlsFocus::Back:
        case ControlsFocus::Reset:
            if (down) {
                return {ControlsFocus::Tabs, 0};
            }
            if (count > 0) {
                return {ControlsFocus::List, count - 1};
            }
            return {has_icons ? ControlsFocus::Icons : ControlsFocus::Tabs, 0};
    }
    return c;
}

std::string controls_label_slug(std::string_view label) {
    std::string out;
    bool gap = false;
    for (const char ch : label) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            if (gap && !out.empty()) {
                out += '_';
            }
            gap = false;
            out += static_cast<char>(std::tolower(c));
        } else {
            gap = true;
        }
    }
    return out;
}

}  // namespace sh::screens
