// SPDX-License-Identifier: GPL-3.0-or-later
// Controls quick-reference screen: the live bindings of every action on one page.
// Rows come from the Controls screen's table (input/bind/bind_table.h), so the page and the
// rebind screen can never disagree; the grouping and the column flow are the pure
// core in engine/input/controls_ref.

#include "engine/text/game_text.h"
#include "ui/screens/pc_controls_ref.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/input/controls_ref.h"
#include "engine/input/input_actions.h"
#include "engine/screens/controls_layout.h"
#include "i18n/localization.h"
#include "input/bind/bind_table.h"
#include "ui/menu/nm_draw.h"

extern "C" int g_DebugThirdPersonCam;

namespace ref = sh::input::ref;
namespace nm = sh::ui::nm;

namespace {

constexpr int kTitleY = 4;
constexpr int kRuleTopY = 20;
constexpr int kRuleBottomY = 207;
constexpr int kHintY = 210;
constexpr int kRuleX0 = 4;
constexpr int kRuleX1 = 316;
// Body glyph scale (Q12): the 12x16 glyphs at half size give 17 rows of 10 px per column.
constexpr int kBodyScale = 2048;
// Frames during which the press that opened the screen is still "held".
constexpr int kInputGrace = 3;

bool s_open = false;
int s_age = 0;

const sh::screens::ControlsScreenColors& colors() {
    static const sh::screens::ControlsScreenColors c;
    return c;
}

int measure_px(std::string_view s) {
    const std::string tmp(s);
    return (sh::i18n::text_width(tmp.c_str()) * kBodyScale) >> 12;
}

// "Move Up" -> "ui.bind.move_up"; "Back / Cancel" -> "ui.bind.back_cancel".
std::string label_key(const char* label) {
    std::string key = "ui.bind.";
    bool sep = false;
    for (const char* p = label; *p != '\0'; ++p) {
        const unsigned char ch = static_cast<unsigned char>(*p);
        if (std::isalnum(ch) != 0) {
            if (sep && key.back() != '.') {
                key += '_';
            }
            sep = false;
            key += static_cast<char>(std::tolower(ch));
        } else {
            sep = true;
        }
    }
    return key;
}

std::string localized_label(const char* label) {
    const std::string key = label_key(label);
    const char* text = sh::i18n::ui_menu_text(key.c_str());
    if (text != nullptr && key != text) {
        return text;
    }
    // Not a ui.bind.* key: the menu text table knows the English label (or leaves it).
    return sh::i18n::menu_text(label);
}

std::string clean_key(const char* value) {
    const std::string clean = nm::font_text(sh::input::bind_display_name(value != nullptr ? value : "").c_str());
    return clean.empty() ? "?" : clean;
}

std::string cell(int row, sh::input::BindColumn first, sh::input::BindColumn second) {
    const std::string a = sh::input::bind_table().value(row, first);
    const std::string b = sh::input::bind_table().value(row, second);
    const bool ua = sh::input::is_unbound(a);
    const bool ub = sh::input::is_unbound(b);
    return sh::input::ref::join_slots(ua ? "" : clean_key(a.c_str()), ub ? "" : clean_key(b.c_str()), "-");
}

ref::Category row_category(const sh::input::BindRow& row) {
    return row.action != sh::input::Action::Count ? ref::category_of(row.action) : ref::Category::Menu;
}

std::vector<ref::Row> build_rows() {
    std::vector<ref::Row> rows;
    using sh::input::BindColumn;
    const int n = sh::input::BindTable::row_count();
    for (int i = 0; i < n; ++i) {
        const sh::input::BindRow* r = sh::input::BindTable::row(i);
        ref::Row row;
        row.category = row_category(*r);
        row.label = localized_label(r->label.c_str());
        row.keyboard = cell(i, BindColumn::Key1, BindColumn::Key2);
        if (r->stick && !r->has(BindColumn::Pad1)) {
            row.pad = sh::i18n::ui_menu_text("ui.controls_ref.stick");
        } else if (!r->has(BindColumn::Pad1)) {
            row.pad = "-";
        } else {
            row.pad = cell(i, BindColumn::Pad1, BindColumn::Pad2);
        }
        row.enabled = sh::input::bind_table().row_enabled(i);
        rows.push_back(std::move(row));
    }
    return rows;
}

void draw_cell(int x, int y, int w, int color, const std::string& text) {
    const std::string fit = ref::fit_ellipsis(text, w, measure_px);
    if (!fit.empty()) {
        nm::text_verbatim(x, y, color, fit.c_str());
    }
}

}  // namespace

extern "C" void Pc_ControlsRef_Open(void) {
    sh::input::bind_table().set_scheme(g_DebugThirdPersonCam != 0 ? sh::input::BindScheme::AltCamera : sh::input::BindScheme::Classic);
    s_open = true;
    s_age = 0;
}

extern "C" int Pc_ControlsRef_IsOpen(void) {
    return s_open ? 1 : 0;
}

extern "C" int Pc_ControlsRef_Update(void) {
    if (!s_open) {
        return 0;
    }
    const nm::Input in = nm::read_input();
    if (s_age++ >= kInputGrace && (in.confirm != 0 || in.cancel != 0 || in.click != 0 || in.right_click != 0)) {
        s_open = false;
        return 0;
    }

    // The scheme can change while the page is open (camera switch), so rows are read each frame.
    const std::vector<ref::Row> rows = build_rows();
    const ref::Params params;
    const ref::Layout layout = ref::compute_layout(rows, params);
    const auto& c = colors();

    nm::black_background();
    std::string title = sh::i18n::ui_menu_text("ui.controls_ref.title");
    title += " - ";
    title += sh::i18n::ui_menu_text(sh::input::bind_table().scheme() == sh::input::BindScheme::AltCamera ? "ui.controls_ref.scheme_altcam" : "ui.controls_ref.scheme_classic");
    nm::text_verbatim(params.margin, kTitleY, c.selected, title.c_str());
    nm::rule(kRuleX0, kRuleX1, kRuleTopY);

    sh::text::game_text().set_scale(kBodyScale);
    nm::keep_text_unfiltered(true);
    for (const ref::Placed& it : layout.items) {
        if (it.kind == ref::Placed::Kind::Heading) {
            const char* head = sh::i18n::ui_menu_text(std::string(ref::category_key(it.category)).c_str());
            nm::text_verbatim(it.x, it.y, c.heading_selected, head);
            continue;
        }
        const ref::Row& r = rows[it.row];
        const int color = r.enabled ? c.normal : c.disabled;
        draw_cell(it.label_x, it.y, it.label_w - 2, color, r.label);
        draw_cell(it.keyboard_x, it.y, it.keyboard_w - 2, color, r.keyboard);
        draw_cell(it.pad_x, it.y, it.pad_w, color, r.pad);
    }

    const char* hint = sh::i18n::ui_menu_text("ui.controls_ref.close_hint");
    nm::text_verbatim(160 - measure_px(hint) / 2, kHintY, c.heading, hint);
    sh::text::game_text().set_scale(4096);
    nm::keep_text_unfiltered(false);
    nm::rule(kRuleX0, kRuleX1, kRuleBottomY);
    return 1;
}
