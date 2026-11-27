// SPDX-License-Identifier: GPL-3.0-or-later
// The bindings screen (bindings_screen.h).
#include "ui/screens/bindings_screen.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"
#include "engine/screens/controls_names.h"
#include "i18n/localization.h"
#include "input/bind/bind_input.h"
#include "input/bind/bind_table.h"
#include "input/bind/pad_chord.h"
#include "input/glyphs/glyph_sheet.h"
#include "input/mouse_pointer.h"

extern "C" const char* PcPort_GetGameDataPath(void);
// Set while one of the alternate cameras is in use: they have their own binds.
extern int g_DebugThirdPersonCam;

namespace sh::ui {

namespace {

namespace gs = sh::game_state;
namespace sc = sh::screens;

using Focus = BindingsCursor::Focus;
using Tab = BindingsCursor::Tab;
using sh::input::BindColumn;
using sh::input::BindRow;
using sh::input::BindTable;
using sh::input::Glyph;
using sh::input::GlyphSet;
using sh::input::NavKey;

BindTable& table() { return sh::input::bind_table(); }

// The name of the icon set the player chose (the gamepad_icons setting).
const char* icon_set_name() { return sh::input::glyph_set_label(sh::input::glyph_sheet().chosen_set()); }

// The width of the space the layout is written in.
constexpr int kSpaceWidth = 320;
// A menu host that has not called frame() for this long is gone.
constexpr std::chrono::milliseconds kHostTimeout{500};

// gamedata/menu/controls_screen.json, read again after `mods reload`; the built-in layout when it is refused.
const sc::ControlsScreenLayout& layout() {
    static sc::ControlsScreenLayout loaded;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        char mods_dir[1024];
        std::string source;
        PcPaths_ExeFile(mods_dir, sizeof(mods_dir), "mods");
        sh::Result<sc::ControlsScreenLayout> read = sc::load_controls_layout(PcPort_GetGameDataPath(), mods_dir, &source);
        if (read) {
            loaded = read.value();
        } else {
            SH_ERR("[BINDS] controls screen layout: %s - built-in layout used", read.error().c_str());
            loaded = sc::ControlsScreenLayout{};
        }
    }
    return loaded;
}

const sc::ControlsScreenColors& colors() { return layout().colors; }
const sc::ControlsScreenTexts& texts() { return layout().texts; }

void cue(int sound) { gs::sfx_play(sound, layout().beep_volume); }

const char* menu_text_of(const std::string& key) { return sh::i18n::ui_menu_text(key.c_str()); }

// The text of `key` when a language has it, else `fallback` (data, English) spelled for the font.
std::string keyed_text(const std::string& key, const std::string& fallback) {
    const char* found = menu_text_of(key);
    if (found != nullptr && key != found) {
        return sh::i18n::menu_text(found);
    }
    return nm::font_text(fallback);
}

// An input's readable name as one line of menu text. A part the font cannot spell is left out: the cell's icon
// stands for it.
std::string name_text(const sc::InputName& name) {
    std::string line;
    for (const sc::InputNamePart& part : name) {
        if (!line.empty()) {
            line += '_';
        }
        line += part.key.empty() ? nm::font_text(part.text) : keyed_text(part.key, part.text);
    }
    return line;
}

std::string action_label(int table_row) {
    const char* label = BindTable::row(table_row)->label.c_str();
    const std::string key = texts().action_prefix + sc::controls_label_slug(label);
    const char* found = menu_text_of(key);
    return sh::i18n::menu_text(found != nullptr && key != found ? found : label);
}

std::array<int, sc::kControlsTabs> tab_widths() {
    std::array<int, sc::kControlsTabs> widths{};
    for (int t = 0; t < sc::kControlsTabs; t++) {
        widths[static_cast<std::size_t>(t)] = nm::text_width(menu_text_of(texts().tabs[static_cast<std::size_t>(t)]));
    }
    return widths;
}

// The footer's Reset is set against the right edge of the values.
int reset_label_x() { return layout().value_right - nm::text_width(menu_text_of(texts().reset)); }

// Where the name of the icon set in the head row starts.
int icon_set_left() { return layout().value_right - nm::text_width(icon_set_name()); }

bool in_row(int pointer_y, int y) { return pointer_y >= y && pointer_y < y + layout().row_h; }

sc::PadLabels pad_labels(GlyphSet icon_set) {
    switch (icon_set) {
        case GlyphSet::PlayStationClassic: return sc::PadLabels::PlayStationClassic;
        case GlyphSet::PlayStation: return sc::PadLabels::PlayStation;
        case GlyphSet::Nintendo: return sc::PadLabels::Nintendo;
        case GlyphSet::Steam: return sc::PadLabels::Steam;
        default: return sc::PadLabels::Xbox;
    }
}

// The icon sheet's cell `glyph` at (x, y) of the layout's space, tinted. The sheet is not part of the menu
// layer's mirroring: an icon stays where the layout puts it.
void draw_icon(Glyph glyph, bool dark, int x, int y, const std::array<int, 3>& tint) {
    const sc::ControlsScreenLayout& l = layout();
    const int lines = gs::screen_interlaced() ? 2 : 1;
    const sh::input::GlyphCell cell = sh::input::glyph_cell(glyph, dark);
    const int u = cell.u, v = cell.v, tex_w = cell.width, screen_w = cell.screen_width;
    const int tex_h = tex_w * sh::input::kGlyphScreenSize / screen_w;

    const int left = x - l.icon_origin_x;
    const int right = left + screen_w;
    const int top = (y - l.icon_origin_y) * lines;
    const int bottom = (y + sh::input::kGlyphScreenSize - l.icon_origin_y) * lines;

    gs::TexturedQuad quad;
    quad.r = static_cast<std::uint8_t>(tint[0]);
    quad.g = static_cast<std::uint8_t>(tint[1]);
    quad.b = static_cast<std::uint8_t>(tint[2]);
    const int xs[4] = {left, right, left, right};
    const int ys[4] = {top, top, bottom, bottom};
    const int us[4] = {u, u + tex_w, u, u + tex_w};
    const int vs[4] = {v, v, v + tex_h, v + tex_h};
    for (int corner = 0; corner < 4; corner++) {
        quad.x[corner] = static_cast<std::int16_t>(xs[corner]);
        quad.y[corner] = static_cast<std::int16_t>(ys[corner]);
        quad.u[corner] = static_cast<std::uint8_t>(us[corner]);
        quad.v[corner] = static_cast<std::uint8_t>(vs[corner]);
    }
    quad.texture_x = sh::input::kGlyphSheetPageX;
    quad.clut_x = sh::input::kGlyphSheetKeyX;
    quad.clut_y = sh::input::kGlyphSheetKeyY;
    gs::menu_textured_quad(l.icon_layer, quad);
}

// The icon of a cell in the cell's text colour; its width, 0 when there is no icon to draw.
int draw_cell_icon(Glyph glyph, int x, int y, int color) {
    if (glyph == Glyph::None || glyph == Glyph::PadEmpty || !sh::input::glyph_sheet().ready()) {
        return 0;
    }
    const sc::ControlsScreenColors& c = colors();
    const sc::ControlsGlyphTints& tints = layout().tints;
    const bool dimmed = color == c.disabled || color == c.stick || color == c.other_device;
    const std::array<int, 3>& tint = color == c.duplicate ? tints.duplicate : dimmed ? tints.disabled : tints.normal;
    draw_icon(glyph, color != c.selected, x, y + layout().icon_dy, tint);
    return sh::input::glyph_cell(glyph, false).screen_width;
}

// A cell's icon, then a name in what is left of the column: `beside_icon` when the icon was drawn, `alone` when
// not. The selected cell pans a name too wide for it; the others are cut.
void draw_value(Glyph glyph, const sc::InputName& alone, const sc::InputName& beside_icon, int slot, int y, int color,
                bool pan) {
    const sc::ControlsScreenLayout& l = layout();
    const sc::ControlsColumn& column = l.columns[static_cast<std::size_t>(slot)];
    const int icon_w = draw_cell_icon(glyph, column.x, y, color);
    const int x = icon_w > 0 ? column.x + icon_w + l.icon_text_dx : column.x;
    const std::string name = name_text(icon_w > 0 ? beside_icon : alone);
    const int end = column.x + column.w;
    if (x < end && !name.empty()) {
        nm::text_fitted(x, y, color, name.c_str(), end - x, pan);
    }
}

// Controller inputs held together, each by its icon (its name where the sheet has none) with " + " between them.
void draw_pad_inputs(const std::vector<std::string>& parts, GlyphSet icon_set, int slot, int y, int color, bool pan) {
    if (parts.size() == 1) {
        const sc::InputName name = sc::pad_input_name(parts[0], pad_labels(icon_set));
        draw_value(sh::input::glyph_for_pad(icon_set, parts[0]), name, name, slot, y, color, pan);
        return;
    }
    const sc::ControlsScreenLayout& l = layout();
    const sc::ControlsColumn& column = l.columns[static_cast<std::size_t>(slot)];
    const int end = column.x + column.w;
    int x = column.x;
    for (std::size_t i = 0; i < parts.size() && x < end; i++) {
        if (i > 0) {
            x += l.icon_text_dx;
            nm::text(x, y, color, "+");
            x += nm::text_width("+") + l.icon_text_dx;
        }
        const int icon_w = draw_cell_icon(sh::input::glyph_for_pad(icon_set, parts[i]), x, y, color);
        if (icon_w > 0) {
            x += icon_w;
            continue;
        }
        const std::string name = name_text(sc::pad_input_name(parts[i], pad_labels(icon_set)));
        if (x < end && !name.empty()) {
            x += nm::text_fitted(x, y, color, name.c_str(), end - x, false);
        }
    }
}

// A key's name and, in `glyph`, its key cap icon when the name is the cap's own text ("W", "F9", "/"): the icon
// reads the same in every language, a word does not.
sc::InputName key_name(const char* value, Glyph& glyph) {
    // The cap of the active layout (QWERTZ's Y is scancode "Z"); the US name when the font cannot spell that cap.
    char cap[sh::menu::kFitLineBytes + 1];
    std::snprintf(cap, sizeof(cap), "%s", sh::input::key_cap_of(value).c_str());
    if (nm::font_text(cap).empty()) {
        std::snprintf(cap, sizeof(cap), "%s", value);
    }
    sc::InputName name = sc::keyboard_input_name(cap);
    glyph = name.size() == 1 && name[0].key.empty() ? sh::input::glyph_for_key(cap) : Glyph::None;
    return name;
}

// A movement row's direction in the stick's order (up, right, down, left), -1 for another row.
int stick_direction(int table_row) {
    const std::string& key = BindTable::row(table_row)->key(BindColumn::Key1);
    if (key.empty()) {
        return -1;
    }
    const auto& rows = texts().stick_rows;
    for (std::size_t i = 0; i < rows.size(); i++) {
        if (rows[i] == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

BindingsScreen& bindings_screen() {
    static BindingsScreen screen;
    return screen;
}

// ---- the tab's rows ------------------------------------------------------------------------------------------------

int BindingsScreen::bind_row(int index) const {
    return index >= 0 && index < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(index)] : -1;
}

BindColumn BindingsScreen::bind_column(int slot) const {
    return static_cast<BindColumn>(static_cast<int>(cursor_.on_gamepad_tab() ? BindColumn::Pad1 : BindColumn::Key1) + slot);
}

// A movement row on the Gamepad tab: the stick and the d-pad are shown in place of binds.
bool BindingsScreen::stick_cell(int index, int slot) const {
    const BindRow* row = BindTable::row(bind_row(index));
    return row != nullptr && cursor_.on_gamepad_tab() && row->stick && !row->has(bind_column(slot));
}

void BindingsScreen::show_tab(Tab tab, bool first) {
    std::vector<sc::ControlsRowCaps> caps;
    for (int i = 0; i < BindTable::row_count(); i++) {
        const BindRow* row = BindTable::row(i);
        sc::ControlsRowCaps c;
        c.keyboard = row->has(BindColumn::Key1);
        c.mouse = row->mouse;
        c.pad = row->has(BindColumn::Pad1);
        c.stick = row->stick;
        caps.push_back(c);
    }
    rows_ = sc::controls_tab_rows(tab, caps);

    const int first_column = static_cast<int>(tab == Tab::Gamepad ? BindColumn::Pad1 : BindColumn::Key1);
    std::vector<BindingsCursor::RowSlots> slots;
    for (const int table_row : rows_) {
        BindingsCursor::RowSlots has{};
        for (int slot = 0; slot < sc::kControlsSlots; slot++) {
            has[static_cast<std::size_t>(slot)] = BindTable::row(table_row)->has(static_cast<BindColumn>(first_column + slot));
        }
        slots.push_back(has);
    }
    if (first) {
        cursor_.reset(tab, std::move(slots), layout().visible_rows);
    } else {
        cursor_.show_tab(tab, std::move(slots), layout().visible_rows);
    }
}

// ---- opening and closing -------------------------------------------------------------------------------------------

void BindingsScreen::start(bool hosted) {
    table().set_scheme(g_DebugThirdPersonCam != 0 ? sh::input::BindScheme::AltCamera : sh::input::BindScheme::Classic);
    show_tab(cursor_.tab(), true);
    capturing_ = false;
    reset_prompt_ = sh::menu::YesNoPrompt{};
    nav_.reset();
    sh::input::glyph_sheet().load();
    hosted_ = hosted;
    last_frame_ = std::chrono::steady_clock::now();
    open_ = true;
    SH_DBG("[BINDS] controls screen open%s: %s scheme", hosted ? " (menu)" : "",
           table().scheme() == sh::input::BindScheme::AltCamera ? "altcam" : "classic");
}

void BindingsScreen::open() { start(false); }

void BindingsScreen::open_from_menu() { start(true); }

void BindingsScreen::open_at_bind(const char* bind_key) {
    cursor_ = BindingsCursor{};  // the Keyboard tab
    start(true);
    for (std::size_t i = 0; i < rows_.size(); i++) {
        const BindRow* row = BindTable::row(rows_[i]);
        if (row != nullptr && row->has(BindColumn::Key1) && row->key(BindColumn::Key1) == bind_key) {
            cursor_.focus_row(static_cast<int>(i));
            break;
        }
    }
}

bool BindingsScreen::is_open() const {
    if (!open_) {
        return false;
    }
    if (hosted_) {
        return std::chrono::steady_clock::now() - last_frame_ < kHostTimeout;
    }
    return gs::game_state() == gs::GameState::OptionScreen;
}

bool BindingsScreen::leave() {
    cue(gs::sfx::kMenuCancel);
    SH_DBG("[BINDS] controls screen closed");
    return false;
}

// ---- actions -------------------------------------------------------------------------------------------------------

void BindingsScreen::step_tab(int dir) {
    show_tab(sc::controls_tab_step(cursor_.tab(), dir), false);
    cue(gs::sfx::kMenuMove);
}

// The selected cell waits for the input to bind.
void BindingsScreen::begin_capture() {
    const int row = bind_row(cursor_.row());
    const BindColumn column = bind_column(cursor_.slot());
    if (!table().cell_usable(row, column) || (sh::input::is_pad_column(column) && !sh::input::controller_connected())) {
        cue(gs::sfx::kMenuError);
        return;
    }
    cue(gs::sfx::kMenuConfirm);
    sh::debug::console_notice(table().rebind_toast(row, column).c_str());
    capturing_ = true;
    const sh::input::CaptureFilter accepts = cursor_.tab() == Tab::Keyboard ? sh::input::CaptureFilter::KeysOnly
                                             : cursor_.tab() == Tab::Mouse  ? sh::input::CaptureFilter::MouseOnly
                                                                            : sh::input::CaptureFilter::KeysAndMouse;
    sh::input::begin_bind_capture(row, column, accepts);
}

void BindingsScreen::clear_bind() {
    const int row = bind_row(cursor_.row());
    const BindColumn column = bind_column(cursor_.slot());
    if (!table().cell_usable(row, column)) {
        cue(gs::sfx::kMenuError);
        return;
    }
    table().set(row, column, "NONE");
    cue(gs::sfx::kMenuCancel);
}

void BindingsScreen::ask_reset() {
    cue(gs::sfx::kMenuConfirm);
    reset_prompt_.open();
}

// ---- the pointer ---------------------------------------------------------------------------------------------------

BindingsScreen::Hit BindingsScreen::hit(const nm::Input& in) const {
    const sc::ControlsScreenLayout& l = layout();
    Hit h;
    if (reset_prompt_.is_open()) {
        const bool on_answers = in_row(in.pointer_y, l.confirm_y + l.confirm_hit_dy) &&
                                in.pointer_x >= l.confirm_hit_x0 && in.pointer_x < l.confirm_hit_x1;
        if (on_answers) {
            h.kind = in.pointer_x < l.confirm_split_x ? Hit::Kind::Yes : Hit::Kind::No;
        }
        return h;
    }

    h.index = sc::controls_tab_hit(l, tab_widths(), in.pointer_x, in.pointer_y);
    if (h.index >= 0) {
        h.kind = Hit::Kind::Tab;
        return h;
    }
    if (cursor_.on_gamepad_tab() && in_row(in.pointer_y, l.head_y)) {
        h.kind = Hit::Kind::Icons;
        return h;
    }
    if (in_row(in.pointer_y, l.footer_y)) {
        // An item reaches from its bullet to a little past its label.
        const int bullet = nm::kBulletTextDx + l.bullet_highlight_left;
        const int back_end = l.text_x + nm::text_width(menu_text_of(texts().back)) + l.footer_hit_right;
        if (in.pointer_x >= l.text_x - bullet && in.pointer_x < back_end) {
            h.kind = Hit::Kind::Back;
        } else if (in.pointer_x >= reset_label_x() - bullet && in.pointer_x < l.value_right + l.footer_hit_right) {
            h.kind = Hit::Kind::Reset;
        }
        return h;
    }
    const sh::menu::ScrollWindow& list = cursor_.list();
    h.index = sc::controls_list_hit(l, list.top(), list.visible(), list.count(), in.pointer_y);
    if (h.index >= 0) {
        h.kind = Hit::Kind::Row;
        h.slot = sc::controls_slot_hit(l, in.pointer_x);
    }
    return h;
}

// Hover moves the focus, a click acts on what is under the pointer, a right click clears a bind. False when a
// click left the screen.
bool BindingsScreen::pointer(const nm::Input& in) {
    if (!in.pointer || !(in.pointer_moved || in.click || in.right_click)) {
        return true;
    }
    const Hit h = hit(in);
    if (h.kind != Hit::Kind::None) {
        nm::snap_highlight();
    }

    if (in.pointer_moved) {
        const Focus focus_before = cursor_.focus();
        const int row_before = cursor_.row();
        const int slot_before = cursor_.slot();
        switch (h.kind) {
            case Hit::Kind::Tab: cursor_.focus_on(Focus::Tabs); break;
            case Hit::Kind::Icons: cursor_.focus_on(Focus::Icons); break;
            case Hit::Kind::Back: cursor_.focus_on(Focus::Back); break;
            case Hit::Kind::Reset: cursor_.focus_on(Focus::Reset); break;
            case Hit::Kind::Row: cursor_.focus_cell(h.index, h.slot); break;
            default: break;
        }
        const bool cell_moved =
            cursor_.focus() == Focus::List && (cursor_.row() != row_before || cursor_.slot() != slot_before);
        if (cursor_.focus() != focus_before || cell_moved) {
            cue(gs::sfx::kMenuMove);
        }
    }

    const bool on_bind = h.kind == Hit::Kind::Row && cursor_.has_slot(h.index, h.slot);
    const bool on_stick = h.kind == Hit::Kind::Row && !on_bind && stick_cell(h.index, h.slot);
    if (in.click) {
        switch (h.kind) {
            case Hit::Kind::Tab:
                cursor_.focus_on(Focus::Tabs);
                if (h.index != static_cast<int>(cursor_.tab())) {
                    show_tab(static_cast<Tab>(h.index), false);
                    cue(gs::sfx::kMenuMove);
                }
                break;
            case Hit::Kind::Icons: {
                // The arrow left of the set's name steps back; anywhere else on the row steps on.
                const int name_left = icon_set_left();
                const bool on_left_arrow =
                    in.pointer_x < name_left && in.pointer_x >= name_left - layout().icon_arrow_hit_w;
                cursor_.focus_on(Focus::Icons);
                sh::input::glyph_sheet().step_chosen_set(on_left_arrow ? -1 : 1);
                cue(gs::sfx::kMenuMove);
                break;
            }
            case Hit::Kind::Back:
                return leave();
            case Hit::Kind::Reset:
                cursor_.focus_on(Focus::Reset);
                ask_reset();
                break;
            case Hit::Kind::Row:
                if (on_bind) {
                    cursor_.focus_cell(h.index, h.slot);
                    begin_capture();
                } else if (on_stick) {
                    cue(gs::sfx::kMenuError);
                }
                break;
            default:
                break;
        }
    } else if (in.right_click) {
        if (on_bind) {
            cursor_.focus_cell(h.index, h.slot);
            clear_bind();
        } else if (on_stick) {
            cue(gs::sfx::kMenuError);
        }
    }
    return true;
}

// ---- a frame of input ----------------------------------------------------------------------------------------------

void BindingsScreen::update_prompt(const sh::input::NavState& pressed, const nm::Input& in) {
    sh::menu::YesNoPrompt::Keys keys;
    keys.left = pressed[NavKey::Left];
    keys.right = pressed[NavKey::Right];
    keys.confirm = pressed[NavKey::Ok];
    keys.cancel = pressed[NavKey::Back];

    if (in.pointer && (in.pointer_moved || in.click)) {
        const Hit h = hit(in);
        if (h.kind == Hit::Kind::Yes || h.kind == Hit::Kind::No) {
            const bool yes = h.kind == Hit::Kind::Yes;
            if (reset_prompt_.on_yes() != yes) {
                cue(gs::sfx::kMenuMove);
            }
            nm::snap_highlight();
            reset_prompt_.point_at(yes);
            keys.confirm = keys.confirm || in.click;
        }
    }
    if (keys.left || keys.right) {
        cue(gs::sfx::kMenuMove);
    }

    switch (reset_prompt_.update(keys)) {
        case sh::menu::YesNoPrompt::Answer::Yes:
            cue(gs::sfx::kMenuConfirm);
            table().reset_scheme();
            break;
        case sh::menu::YesNoPrompt::Answer::No:
            cue(gs::sfx::kMenuCancel);
            break;
        case sh::menu::YesNoPrompt::Answer::Pending:
            break;
    }
}

bool BindingsScreen::update() {
    if (!open_) {
        return false;
    }
    last_frame_ = std::chrono::steady_clock::now();

    if (capturing_) {
        const sh::input::BindCapture::Result captured = sh::input::update_bind_capture();
        if (captured.status != sh::input::BindCapture::Status::Waiting) {
            capturing_ = false;
            if (captured.status == sh::input::BindCapture::Status::Cancelled) {
                cue(gs::sfx::kMenuCancel);
            } else {
                table().set(bind_row(cursor_.row()), bind_column(cursor_.slot()), captured.name);
                cue(gs::sfx::kMenuConfirm);
            }
            // The press that was captured must not move the cursor as well.
            nav_.reset();
        }
        return true;
    }

    const sh::input::NavState pressed = sh::input::poll_menu_nav(nav_);
    const nm::Input in = nm::read_input();

    if (reset_prompt_.is_open()) {
        update_prompt(pressed, in);
        return true;
    }
    if (pressed[NavKey::Back]) {
        return leave();
    }
    if (pressed[NavKey::TabPrev] || pressed[NavKey::TabNext]) {
        step_tab(pressed[NavKey::TabPrev] ? -1 : 1);
        return true;
    }

    if (in.wheel != 0) {
        cursor_.wheel(in.wheel);
    }
    if (!pointer(in)) {
        return false;
    }
    if (capturing_ || reset_prompt_.is_open()) {
        return true;
    }

    if (pressed[NavKey::Up] || pressed[NavKey::Down]) {
        cursor_.move_vertical(pressed[NavKey::Up] ? -1 : 1);
        cue(gs::sfx::kMenuMove);
    }
    if (pressed[NavKey::Left] || pressed[NavKey::Right]) {
        const int dir = pressed[NavKey::Left] ? -1 : 1;
        switch (cursor_.focus()) {
            case Focus::Tabs:
                step_tab(dir);
                break;
            case Focus::Icons:
                sh::input::glyph_sheet().step_chosen_set(dir);
                cue(gs::sfx::kMenuMove);
                break;
            case Focus::List:
                if (cursor_.move_slot(dir)) {
                    cue(gs::sfx::kMenuMove);
                }
                break;
            case Focus::Back:
            case Focus::Reset:
                cursor_.swap_footer();
                cue(gs::sfx::kMenuMove);
                break;
        }
    }

    if (pressed[NavKey::Ok]) {
        switch (cursor_.focus()) {
            case Focus::Tabs:
                step_tab(1);
                break;
            case Focus::Icons:
                sh::input::glyph_sheet().step_chosen_set(1);
                cue(gs::sfx::kMenuMove);
                break;
            case Focus::List:
                begin_capture();
                break;
            case Focus::Back:
                return leave();
            case Focus::Reset:
                ask_reset();
                break;
        }
    } else if (pressed[NavKey::Clear]) {
        if (cursor_.focus() == Focus::List) {
            clear_bind();
        } else {
            cue(gs::sfx::kMenuError);
        }
    }
    return true;
}

bool BindingsScreen::frame() {
    const bool still_open = update();
    if (still_open) {
        draw();
    } else {
        close();
    }
    sh::input::mouse_pointer().show();
    return still_open;
}

// ---- drawing -------------------------------------------------------------------------------------------------------

void BindingsScreen::draw_stick(int table_row, int slot, int y) {
    const int direction = stick_direction(table_row);
    if (direction < 0) {
        return;
    }
    const bool dpad = slot != 0;
    const int column_x = layout().columns[static_cast<std::size_t>(slot)].x;
    const Glyph glyph =
        sh::input::glyph_for_stick(icon_set_, static_cast<sh::input::GlyphDirection>(direction), dpad);
    // The icon says it; the name is for a screen without icons.
    if (draw_cell_icon(glyph, column_x, y, colors().stick) == 0) {
        const sc::InputName name = sc::stick_input_name(direction, dpad);
        draw_value(Glyph::None, name, name, slot, y, colors().stick, false);
    }
}

void BindingsScreen::draw_cell(int index, int slot, int y, bool selected) {
    const sc::ControlsScreenColors& c = colors();
    const int table_row = bind_row(index);
    const BindColumn column = bind_column(slot);
    const int column_x = layout().columns[static_cast<std::size_t>(slot)].x;

    if (!cursor_.has_slot(index, slot)) {
        if (stick_cell(index, slot)) {
            draw_stick(table_row, slot, y);
        }
        return;
    }
    if (selected && capturing_) {
        // What the controller holds so far, live: the capture takes it when the buttons are let go.
        const std::vector<std::string>& held = sh::input::bind_capture_pad_held();
        if (sh::input::is_pad_column(column) && !held.empty()) {
            draw_pad_inputs(held, icon_set_, slot, y, c.selected, false);
        } else {
            nm::text(column_x, y, c.selected, texts().listening.c_str());
        }
        return;
    }

    const std::string bound = table().value(table_row, column);
    const char* value = bound.c_str();
    const sc::BindDevice device =
        sc::bind_device(sh::input::is_unbound(bound) ? "" : value, sh::input::is_pad_column(column));
    const sc::BindDevice tab_device = cursor_.on_gamepad_tab()           ? sc::BindDevice::Pad
                                      : cursor_.tab() == Tab::Mouse      ? sc::BindDevice::Mouse
                                                                         : sc::BindDevice::Keyboard;
    int color = c.normal;
    if (selected) {
        color = c.selected;
    } else if (!table().row_enabled(table_row)) {
        color = c.disabled;
    } else if (device != sc::BindDevice::None && device != tab_device) {
        color = c.other_device;
    } else if (table().is_duplicate(table_row, column)) {
        color = c.duplicate;
    }

    switch (device) {
        case sc::BindDevice::None:
            nm::text(column_x, y, color, texts().unbound.c_str());
            break;
        case sc::BindDevice::Pad: {
            const std::vector<std::string> parts = sh::input::chord_parts(bound);
            draw_pad_inputs(parts.size() > 1 ? parts : std::vector<std::string>{bound}, icon_set_, slot, y, color,
                            selected);
            break;
        }
        case sc::BindDevice::Mouse:
            draw_value(sh::input::glyph_for_key(value), sc::mouse_input_name(value), sc::mouse_short_name(value), slot, y,
                       color, selected);
            break;
        case sc::BindDevice::Keyboard: {
            Glyph glyph = Glyph::None;
            const sc::InputName name = key_name(value, glyph);
            draw_value(glyph, name, sc::InputName{}, slot, y, color, selected);
            break;
        }
    }
}

void BindingsScreen::draw_row(int index, int y) {
    const sc::ControlsScreenLayout& l = layout();
    const int table_row = bind_row(index);
    const bool selected = cursor_.on_row(index);
    const int color = table().row_enabled(table_row) ? colors().label : colors().disabled;
    const std::string label = action_label(table_row);
    const int bullet_x = l.text_x - nm::kBulletTextDx;

    nm::bullet(bullet_x, y, selected);
    const int label_w = nm::text_fitted(l.text_x, y, color, label.c_str(), l.label_w, selected);
    for (int slot = 0; slot < sc::kControlsSlots; slot++) {
        draw_cell(index, slot, y, selected && slot == cursor_.slot());
    }
    if (selected) {
        nm::highlight(bullet_x - l.bullet_highlight_left, l.text_x + label_w + l.bullet_highlight_right,
                      y + l.highlight_dy);
    }
}

void BindingsScreen::draw_footer_item(int x, int y, bool selected, const char* label) {
    const sc::ControlsScreenLayout& l = layout();
    const int bullet_x = x - nm::kBulletTextDx;
    nm::bullet(bullet_x, y, selected);
    nm::text(x, y, colors().label, label);
    if (selected) {
        nm::highlight(bullet_x - l.bullet_highlight_left, x + nm::text_width(label) + l.bullet_highlight_right,
                      y + l.highlight_dy);
    }
}

void BindingsScreen::draw_tabs() {
    const sc::ControlsScreenLayout& l = layout();
    const std::array<int, sc::kControlsTabs> widths = tab_widths();
    for (int t = 0; t < sc::kControlsTabs; t++) {
        const int w = widths[static_cast<std::size_t>(t)];
        const int x = sc::controls_tab_left(l, t, w);
        const bool current = t == static_cast<int>(cursor_.tab());
        nm::text(x, l.tabs_y, current ? colors().heading_selected : colors().heading,
                 menu_text_of(texts().tabs[static_cast<std::size_t>(t)]));
        if (current && cursor_.focus() == Focus::Tabs) {
            nm::highlight(x - l.bullet_highlight_left, x + w + l.bullet_highlight_right, l.tabs_y + l.highlight_dy);
        }
    }
}

// Under the tabs: the icon set on the Gamepad tab, the names of the two slots on the others.
void BindingsScreen::draw_head() {
    const sc::ControlsScreenLayout& l = layout();
    if (cursor_.on_gamepad_tab()) {
        draw_footer_item(l.text_x, l.head_y, cursor_.focus() == Focus::Icons, menu_text_of(texts().gamepad_icons));
        nm::text_right(l.value_right, l.head_y, colors().value, icon_set_name());
        nm::value_arrows(icon_set_left(), l.value_right, l.head_y);
        return;
    }

    // Each name is centred over its column; one wider than that moves left, as far as the name before it, and the
    // last may reach the edge of the screen.
    int previous_end = l.text_x;
    for (int slot = 0; slot < sc::kControlsSlots; slot++) {
        const sc::ControlsColumn& column = l.columns[static_cast<std::size_t>(slot)];
        const bool selected = cursor_.focus() == Focus::List && cursor_.slot() == slot;
        const std::string name = sh::i18n::menu_text(menu_text_of(slot == 0 ? texts().primary : texts().secondary));
        const int limit = slot + 1 < sc::kControlsSlots ? l.columns[static_cast<std::size_t>(slot) + 1].x - l.cell_hit_pad
                                                         : kSpaceWidth;
        const int w = sh::i18n::text_width(name.c_str());
        int x = column.x + (column.w - w) / 2;
        if (x + w > limit) {
            x = limit - w;
        }
        if (x < previous_end) {
            x = previous_end;
        }
        const int color = selected ? colors().heading_selected : colors().heading;
        previous_end = x + nm::text_fitted(x, l.head_y, color, name.c_str(), limit - x, false) + l.bullet_highlight_right;
    }
}

void BindingsScreen::draw() {
    if (!open_) {
        return;
    }
    const sc::ControlsScreenLayout& l = layout();
    nm::black_background();
    if (reset_prompt_.is_open()) {
        nm::yes_no(reset_prompt_, menu_text_of(texts().reset_controls), l.confirm_y);
        return;
    }

    icon_set_ = sh::input::glyph_sheet().active_set();
    draw_tabs();
    nm::rule(l.rule_x0, l.rule_x1, l.rule1_y);
    draw_head();
    nm::rule(l.rule_x0, l.rule_x1, l.rule2_y);

    const sh::menu::ScrollWindow& list = cursor_.list();
    for (int index = list.top(); index < list.bottom(); index++) {
        draw_row(index, l.list_y + (index - list.top()) * l.row_pitch);
    }
    if (list.count() > list.visible()) {
        nm::scroll_arrow(l.arrow_x, l.arrow_up_y, true, list.can_scroll_up());
        nm::scroll_arrow(l.arrow_x, l.arrow_down_y, false, list.can_scroll_down());
    }

    nm::rule(l.rule_x0, l.rule_x1, l.rule3_y);
    draw_footer_item(l.text_x, l.footer_y, cursor_.focus() == Focus::Back, menu_text_of(texts().back));
    draw_footer_item(reset_label_x(), l.footer_y, cursor_.focus() == Focus::Reset, menu_text_of(texts().reset));
}

}  // namespace sh::ui
