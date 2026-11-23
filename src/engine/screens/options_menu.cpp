// SPDX-License-Identifier: GPL-3.0-or-later
// The options screen and gamedata/menu/options.json.

#include "engine/screens/options_menu.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using State = OptionsState;
using Kind = OptionsRow::Kind;

std::int32_t state_number(State s) { return static_cast<std::int32_t>(s); }

MenuPoint shifted(const MenuPoint& p, std::int32_t dx, std::int32_t dy) { return {p.x + dx, p.y + dy}; }

void shift_triangle(const MenuPoint (&in)[3], std::int32_t dx, MenuPoint (&out)[3]) {
    for (int k = 0; k < 3; k++) {
        out[k] = {static_cast<std::int16_t>(in[k].x + dx), in[k].y};
    }
}

bool language_row(const OptionsRow& row, OptionsHost& host) {
    return row.language_label.has_value() && host.language_row_active();
}

}  // namespace

void SelectionHighlight::draw(const OptionsList& list, std::int32_t rows, std::int32_t selected,
                              std::int32_t previous, std::int32_t width_previous, std::int32_t width_selected,
                              std::int32_t timer, bool& snap, std::int32_t row_pitch, OptionsHost& host,
                              const MenuWidgets& widgets, MenuSink& sink) {
    // The ends are kept as 16-bit screen coordinates, like the primitives they become.
    if (timer == 0 || snap) {
        snap = false;
        from_ = {static_cast<std::int16_t>(width_previous + list.underline_x),
                 static_cast<std::int16_t>(static_cast<std::uint16_t>(previous) * row_pitch - list.underline_y)};
        to_ = {static_cast<std::int16_t>(width_selected + list.underline_x),
               static_cast<std::int16_t>(static_cast<std::uint16_t>(selected) * row_pitch - list.underline_y)};
    }
    const auto alpha = static_cast<std::int16_t>(host.sine(timer << 7));
    MenuPoint end;
    end.x = static_cast<std::int16_t>(from_.x + (((to_.x - from_.x) * alpha) >> 12));
    end.y = static_cast<std::int16_t>(from_.y + (((to_.y - from_.y) * alpha) >> 12) + row_pitch);
    widgets.underline({list.underline_x, end.y}, end, true, sink);

    for (std::int32_t i = 0; i < rows; i++) {
        MenuPoint front[4];
        MenuPoint back[4];
        for (int k = 0; k < 4; k++) {
            front[k] = shifted(list.bullet_front[k], 0, i * row_pitch);
            back[k] = shifted(list.bullet_back[k], 0, i * row_pitch);
        }
        const bool inactive = i != selected;
        widgets.bullet(front, false, inactive, sink);
        widgets.bullet(back, true, inactive, sink);
    }
}

OptionsScreen::OptionsScreen(const OptionsLayout& layout, const MenuWidgets& widgets)
    : layout_(layout), widgets_(widgets) {
    main_.row_data.assign(layout_.main.rows.size(), 0);
    extra_.row_data.assign(layout_.extra.rows.size(), 0);
    for (std::size_t i = 0; i < layout_.main.rows.size(); i++) {
        if (layout_.main.rows[i].kind == Kind::Leave) {
            main_home_ = static_cast<std::int32_t>(i);
            break;
        }
    }
}

bool OptionsScreen::advance_timer() {
    if (layout_.highlight_frames - 1 < timer_) {
        timer_ = layout_.highlight_frames;
    } else {
        timer_++;
    }
    return timer_ == layout_.highlight_frames;
}

void OptionsScreen::enter(OptionsHost& host) {
    host.prepare_screen();
    host.set_fade({FadeState::InSteps, false});
    main_.selected = main_home_;
    main_.previous = 0;
    extra_.selected = 0;
    extra_.previous = 0;
    timer_ = 0;
    for (ListState* state : {&main_, &extra_}) {
        const OptionsList& list = state == &main_ ? layout_.main : layout_.extra;
        for (std::size_t i = 0; i < list.rows.size(); i++) {
            const OptionsRow& row = list.rows[i];
            if (row.kind == Kind::Count) {
                state->row_data[i] = host.choice_count(row.setting);
            } else if (row.kind == Kind::List) {
                // A stored value the list does not know keeps the previous index.
                const std::int32_t code = host.value(row.setting);
                for (std::size_t k = 0; k < row.codes.size(); k++) {
                    if (row.codes[k] == code) {
                        state->row_data[i] = static_cast<std::int32_t>(k);
                        break;
                    }
                }
            }
        }
    }
    extra_rows_ = 0;
    const bool unlocked = host.extras_unlocked();
    for (const OptionsRow& row : layout_.extra.rows) {
        if (row.locked && !unlocked) {
            break;
        }
        extra_rows_++;
    }
    host.set_state(state_number(State::Main));
}

void OptionsScreen::update(OptionsHost& host, MenuSink& sink) {
    if (host.from_gameplay()) {
        host.update_bgm_track();
    }
    // The pause menu keeps the clock stopped, on the screens it opens too.
    if (!host.from_title() && !host.opened_from_pause()) {
        host.update_play_timer();
    }

    switch (static_cast<State>(host.state())) {
        case State::EnterMain:
            enter(host);
            break;

        case State::LeaveBrightness:
        case State::LeaveController:
            host.set_state(state_number(State::Main));
            break;

        case State::EnterBrightness:
            if (host.fade_finished()) {
                host.wait_for_loads();
                host.set_state(state_number(State::Brightness));
            }
            break;

        case State::Brightness:
            host.update_submenu(Submenu::Brightness);
            break;

        case State::EnterController:
            if (host.fade_finished()) {
                host.set_state(state_number(State::Controller));
                // Once per visit: a visit left by a soft reset never closes the screen.
                host.open_submenu(Submenu::Controller);
            }
            break;

        case State::Controller:
            host.update_submenu(Submenu::Controller);
            break;

        case State::Leave:
            host.set_fade({FadeState::OutStart, false});
            host.set_state(state_number(State::LeaveMain));
            break;

        case State::LeaveMain:
            if (host.fade_finished()) {
                host.leave_screen();
            }
            break;

        case State::EnterExtra:
            if (host.fade_finished()) {
                host.set_state(state_number(State::Extra));
                host.set_fade({FadeState::InSteps, false});
                timer_ = 0;
            }
            break;

        case State::LeaveExtra:
            if (host.fade_finished()) {
                host.set_state(state_number(State::EnterMain));
                host.set_fade({FadeState::InSteps, false});
            }
            break;

        default:
            break;
    }

    switch (static_cast<State>(host.state())) {
        case State::Main:
        case State::Leave:
        case State::LeaveMain:
        case State::EnterBrightness:
        case State::EnterController:
        case State::EnterExtra:
            if (!layout_.tree.empty()) {
                host.update_tree(host.state() == state_number(State::Main));
                break;
            }
            list_update(layout_.main, main_, static_cast<std::int32_t>(layout_.main.rows.size()), true, host, sink);
            break;

        case State::Extra:
        case State::LeaveExtra:
            list_update(layout_.extra, extra_, extra_rows_, false, host, sink);
            break;

        default:
            break;
    }
}

void OptionsScreen::list_update(const OptionsList& list, ListState& state, std::int32_t rows, bool main,
                                OptionsHost& host, MenuSink& sink) {
    draw_labels(list, rows, host);
    draw_values(list, state, rows, host, sink);
    state.highlight.draw(list, rows, state.selected, state.previous, list.rows[state.previous].underline,
                         list.rows[state.selected].underline, timer_, snap_, layout_.row_pitch, host, widgets_,
                         sink);
    widgets_.vignette(sink);
    host.draw_background();
    if (main) {
        draw_volume_bars(list, host, sink);
    }
    host.show_mouse_cursor();

    const State active = main ? State::Main : State::Extra;
    if (host.state() != state_number(active)) {
        return;
    }

    if (advance_timer()) {
        state.previous = state.selected;
        mouse(list, state, rows, host);

        // The option button leaves straight to the game when the screen was opened from it.
        if (host.from_gameplay() && !host.clicked(MenuButton::Enter) && host.clicked(MenuButton::Option)) {
            host.play_sfx(MenuSfx::Cancel);
            host.set_state(state_number(State::Leave));
            return;
        }

        if (host.pulsed(MenuButton::Up)) {
            host.play_sfx(MenuSfx::Move);
            state.selected = (state.selected + rows - 1) % rows;
            timer_ = 0;
        }
        if (host.pulsed(MenuButton::Down)) {
            host.play_sfx(MenuSfx::Move);
            state.selected = (state.selected + 1) % rows;
            timer_ = 0;
        }
        row_input(list.rows[state.selected], state.row_data[state.selected], host);
    } else if (main) {
        return;
    }

    if (main) {
        // A shoulder button switches to the extra options.
        if (host.clicked(MenuButton::Shoulder)) {
            if (host.state() == state_number(State::EnterExtra)) {
                return;
            }
            host.play_sfx(MenuSfx::Confirm);
            host.set_fade({FadeState::OutStart, false});
            host.set_state(state_number(State::EnterExtra));
        }
        // Cancel first jumps to the leave row.
        if (host.state() != state_number(State::EnterExtra) && state.selected != main_home_ &&
            !host.clicked(MenuButton::Enter) && host.clicked(MenuButton::Cancel)) {
            host.play_sfx(MenuSfx::Cancel);
            timer_ = 0;
            state.selected = main_home_;
        }
        return;
    }

    if ((host.clicked(MenuButton::Cancel) || host.clicked(MenuButton::Shoulder)) &&
        host.state() != state_number(State::LeaveExtra)) {
        host.play_sfx(host.clicked(MenuButton::Cancel) ? MenuSfx::Cancel : MenuSfx::Confirm);
        host.set_fade({FadeState::OutStart, false});
        host.set_state(state_number(State::LeaveExtra));
    }
}

void OptionsScreen::draw_labels(const OptionsList& list, std::int32_t rows, OptionsHost& host) const {
    host.text_color(TextColor::White);
    host.text_position(list.heading_pos.x, list.heading_pos.y);
    host.text_layer(layout_.text_layer);
    host.text_draw(list.heading, layout_.label_length);
    for (std::int32_t i = 0; i < rows; i++) {
        const OptionsRow& row = list.rows[i];
        host.text_position(list.rows_x, list.rows_y + i * layout_.row_pitch);
        host.text_layer(layout_.text_layer);
        host.text_draw(language_row(row, host) ? *row.language_label : row.label, layout_.label_length);
    }
    if (list.reset_text_layer) {
        host.text_reset_layer();
    }
}

void OptionsScreen::draw_values(const OptionsList& list, const ListState& state, std::int32_t rows,
                                OptionsHost& host, MenuSink& sink) const {
    host.text_color(TextColor::White);

    const OptionsRow& selected = list.rows[state.selected];
    if (selected.has_arrows) {
        // Language names are wider than the values the arrows were placed for.
        const std::int32_t spread =
            language_row(selected, host) ? host.language_arrow_spread(host.language_slot()) : 0;
        const std::int32_t timer = host.frame_counter();
        for (int i = 0; i < 2; i++) {
            MenuPoint v[3];
            shift_triangle(selected.arrows[i], i == 0 ? -spread : spread, v);
            widgets_.arrow(v, true, timer, sink);
        }
        // The outline marks the side being pushed.
        if (host.held(MenuButton::Left)) {
            MenuPoint v[3];
            shift_triangle(selected.arrow_borders[0], -spread, v);
            widgets_.arrow(v, false, timer, sink);
        }
        if (host.held(MenuButton::Right)) {
            MenuPoint v[3];
            shift_triangle(selected.arrow_borders[1], spread, v);
            widgets_.arrow(v, false, timer, sink);
        }
    }

    for (std::int32_t i = 0; i < rows; i++) {
        const OptionsRow& row = list.rows[i];
        const std::int32_t y = list.rows_y + i * layout_.row_pitch;
        const OptionsValue* value = nullptr;
        switch (row.kind) {
            case Kind::Toggle:
                if (language_row(row, host)) {
                    const std::int32_t slot = host.language_slot();
                    host.text_position(host.language_name_x(slot), y);
                    host.text_draw(host.language_name(slot), host.language_name_length(slot));
                    continue;
                }
                value = &row.values[host.value(row.setting) != 0 ? 1 : 0];
                break;
            case Kind::List:
                value = &row.values[state.row_data[i]];
                break;
            case Kind::Count: {
                const std::int32_t v = host.value(row.setting);
                if (v >= 0 && v < static_cast<std::int32_t>(row.values.size())) {
                    value = &row.values[v];
                }
                break;
            }
            case Kind::Choice: {
                std::int32_t v = host.value(row.setting);
                if (v < 0 || v >= static_cast<std::int32_t>(row.values.size())) {
                    v = 0;
                }
                const std::string& text = row.values[v].text;
                host.text_position(row.align_right_x - static_cast<std::int32_t>(text.size()) * row.glyph_width, y);
                host.text_draw(text, layout_.value_length);
                continue;
            }
            default:
                break;
        }
        if (value != nullptr) {
            host.text_position(value->x, y);
            host.text_draw(value->text, layout_.value_length);
        }
    }
}

void OptionsScreen::draw_volume_bars(const OptionsList& list, OptionsHost& host, MenuSink& sink) const {
    const VolumeBarLayout& bar = layout_.volume_bar;
    for (const OptionsRow& row : list.rows) {
        if (row.kind != Kind::Volume) {
            continue;
        }
        const std::int32_t volume = host.volume(row.setting);
        const std::int32_t full = volume / bar.per_notch;
        const std::uint32_t part = static_cast<std::uint32_t>(volume % bar.per_notch) * bar.partial_step + bar.partial;
        for (std::int32_t i = 0; i < bar.notches; i++) {
            // Inner part first, then the darker outline around it.
            for (std::int32_t j = 1; j >= 0; j--) {
                const std::uint32_t base = i < full ? bar.lit : i > full ? bar.unlit : part;
                const std::uint32_t c = (base + bar.inner_add * static_cast<std::uint32_t>(j)) & 0xFF;
                MenuPrim r;
                r.kind = MenuPrim::Kind::FlatQuad;
                r.layer = MenuPrim::Layer::Bar;
                r.rgb[0] = c | c << 8 | c << 16;
                const std::int32_t left = bar.x + i * bar.pitch + j;
                const std::int32_t right = bar.x + i * bar.pitch - j + bar.width;
                const std::int32_t top = row.bar_y + j;
                const std::int32_t bottom = row.bar_y + bar.height - j;
                r.x[0] = left;
                r.y[0] = top;
                r.x[1] = left;
                r.y[1] = bottom;
                r.x[2] = right;
                r.y[2] = top;
                r.x[3] = right;
                r.y[3] = bottom;
                sink.prim(r);
            }
        }
    }
}

void OptionsScreen::mouse(const OptionsList& list, ListState& state, std::int32_t rows, OptionsHost& host) {
    std::int32_t mx = 0;
    std::int32_t my = 0;
    if (!host.mouse_ui_position(mx, my)) {
        return;
    }
    std::int32_t hit = -1;
    for (std::int32_t i = 0; i < rows; i++) {
        const std::int32_t top = list.rows_y + i * layout_.row_pitch + layout_.mouse_row_top;
        if (my >= top && my < top + layout_.row_pitch) {
            hit = i;
            break;
        }
    }

    // Hover selects at once, so the click that follows is not held back by the slide.
    if (hit >= 0 && host.mouse_moved() && hit != state.selected) {
        state.selected = hit;
        state.previous = hit;
        snap_ = true;
        host.play_sfx(MenuSfx::Move);
    }

    if (hit >= 0 && hit == state.selected) {
        const std::int32_t wheel = host.mouse_wheel_step();
        const OptionsRow& row = list.rows[hit];
        if (row.kind == Kind::Leave || row.kind == Kind::Submenu) {
            if (host.mouse_left_clicked()) {
                host.inject_enter();
            }
        } else if (row.kind == Kind::Volume) {
            // Dragging walks the volume one step per frame towards the pointed notch.
            const VolumeBarLayout& bar = layout_.volume_bar;
            if (host.mouse_left_held() && mx >= bar.mouse_grab_x) {
                const std::int32_t current = host.volume(row.setting);
                std::int32_t lit = 0;
                if (mx >= bar.mouse_x) {
                    lit = (mx - bar.mouse_x) / bar.pitch + 1;
                    if (lit < 1) lit = 1;
                    if (lit > bar.notches) lit = bar.notches;
                }
                if (lit > current / bar.per_notch) {
                    host.inject_direction(1);
                } else if (lit < current / bar.per_notch) {
                    host.inject_direction(-1);
                }
            } else if (wheel != 0) {
                host.inject_direction(wheel);
            }
        } else if (host.mouse_left_clicked()) {
            // The clicked side of the value is the direction, as the arrows show.
            host.inject_direction(mx < list.mouse_split_x ? -1 : 1);
        } else if (wheel != 0) {
            host.inject_direction(wheel);
        }
    }

    if (host.mouse_right_clicked()) {
        host.inject_cancel();
    }
}

void OptionsScreen::row_input(const OptionsRow& row, std::int32_t& data, OptionsHost& host) {
    switch (row.kind) {
        case Kind::Leave:
            if (host.clicked(MenuButton::Enter) || host.clicked(MenuButton::Cancel)) {
                host.play_sfx(MenuSfx::Cancel);
                host.set_state(state_number(State::Leave));
            }
            break;

        case Kind::Submenu:
            if (host.clicked(MenuButton::Enter)) {
                host.play_sfx(MenuSfx::Confirm);
                host.load_submenu(row.submenu);
                host.set_fade({FadeState::OutStart, false});
                switch (row.submenu) {
                    case Submenu::Brightness: host.set_state(state_number(State::EnterBrightness)); break;
                    case Submenu::Controller: host.set_state(state_number(State::EnterController)); break;
                }
            }
            break;

        case Kind::Toggle:
            if (language_row(row, host)) {
                const std::int32_t count = host.language_count();
                const std::int32_t slot = host.language_slot();
                if (host.clicked(MenuButton::Right)) {
                    host.play_sfx(MenuSfx::Move);
                    host.set_language_slot((slot + 1) % count);
                } else if (host.clicked(MenuButton::Left)) {
                    host.play_sfx(MenuSfx::Move);
                    host.set_language_slot((slot + count - 1) % count);
                }
                break;
            }
            if (host.clicked(MenuButton::Right) || host.clicked(MenuButton::Left)) {
                host.play_sfx(MenuSfx::Move);
                host.set_value(row.setting, host.value(row.setting) == 0 ? 1 : 0);
            }
            break;

        case Kind::List: {
            const auto count = static_cast<std::int32_t>(row.codes.size());
            if (host.clicked(MenuButton::Right)) {
                host.play_sfx(MenuSfx::Move);
                data++;
            }
            if (host.clicked(MenuButton::Left)) {
                host.play_sfx(MenuSfx::Move);
                data += count - 1;
            }
            data %= count;
            host.set_value(row.setting, row.codes[data]);
            break;
        }

        case Kind::Count: {
            std::int32_t v = host.value(row.setting);
            if (host.clicked(MenuButton::Right)) {
                host.play_sfx(MenuSfx::Move);
                v++;
            }
            if (host.clicked(MenuButton::Left)) {
                host.play_sfx(MenuSfx::Move);
                v += data - 1;
            }
            if (data > 0) {
                host.set_value(row.setting, v % data);
            }
            break;
        }

        case Kind::Choice: {
            const std::int32_t dir =
                host.clicked(MenuButton::Right) ? 1 : host.clicked(MenuButton::Left) ? -1 : 0;
            if (dir != 0) {
                host.play_sfx(MenuSfx::Move);
                host.set_value(row.setting, (host.value(row.setting) + dir + row.cycle) % row.cycle);
            }
            break;
        }

        case Kind::Volume:
            volume_input(row, host);
            break;
    }
}

void OptionsScreen::volume_input(const OptionsRow& row, OptionsHost& host) const {
    const VolumeBarLayout& bar = layout_.volume_bar;
    const std::int32_t max = bar.max_volume();
    std::int32_t v = host.volume(row.setting);
    const bool right = host.pulsed(MenuButton::Right);
    const bool left = host.pulsed(MenuButton::Left);
    if ((v < max && right) || (v > 0 && left)) {
        host.play_sound(MenuSfx::Move);
    }
    if ((v == max && host.clicked(MenuButton::Right)) || (v == 0 && host.clicked(MenuButton::Left))) {
        host.play_sound(MenuSfx::Error);
    }
    if (right) {
        v += bar.per_notch;
    }
    if (left) {
        v -= bar.per_notch;
    }
    if (v < 0) v = 0;
    if (v > max) v = max;
    host.set_volume(row.setting, v);
}

namespace {

using json::Value;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string at(const Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

const Value& member(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        fail(at(parent, "missing \"" + std::string(key) + "\""));
    }
    return *v;
}

void reject_unknown(const Value& object, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(at(object, "an object was expected"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key.rfind("_", 0) == 0 || key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\""));
        }
    }
}

std::int32_t int_value(const Value& v, std::int32_t lo, std::int32_t hi, std::string_view what) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, std::string(what) + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi)));
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t int_of(const Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    return int_value(member(parent, key), lo, hi, "\"" + std::string(key) + "\"");
}

std::string string_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_string()) {
        fail(at(v, "\"" + std::string(key) + "\" must be a string"));
    }
    return v.string();
}

bool bool_of(const Value& parent, std::string_view key, bool fallback) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        return fallback;
    }
    if (!v->is_bool()) {
        fail(at(*v, "\"" + std::string(key) + "\" must be true or false"));
    }
    return v->boolean();
}

MenuPoint point_of(const Value& p) {
    if (!p.is_array() || p.array().size() != 2) {
        fail(at(p, "a point is [x, y]"));
    }
    return {int_value(p.array()[0], -4096, 4096, "x"), int_value(p.array()[1], -4096, 4096, "y")};
}

template <std::size_t N>
void points_of(const Value& v, MenuPoint (&out)[N]) {
    if (!v.is_array() || v.array().size() != N) {
        fail(at(v, "expected " + std::to_string(N) + " [x, y] points"));
    }
    for (std::size_t k = 0; k < N; k++) {
        out[k] = point_of(v.array()[k]);
    }
}

void arrow_pair_of(const Value& parent, std::string_view key, MenuPoint (&out)[2][3]) {
    const Value& v = member(parent, key);
    if (!v.is_array() || v.array().size() != 2) {
        fail(at(v, "\"" + std::string(key) + "\" lists the left and the right arrow"));
    }
    points_of(v.array()[0], out[0]);
    points_of(v.array()[1], out[1]);
}

std::uint32_t gray_of(const Value& parent, std::string_view key) {
    return static_cast<std::uint32_t>(int_of(parent, key, 0, 255));
}

OptionsRow row_of(const Value& r) {
    reject_unknown(r, {"label", "kind", "underline", "submenu", "setting", "values", "codes", "cycle",
                       "align_right_x", "glyph_width", "bar_y", "locked", "language_label", "arrows",
                       "arrow_borders"});
    OptionsRow row;
    row.label = string_of(r, "label");
    row.underline = int_of(r, "underline", 0, 4096);
    const std::string kind = string_of(r, "kind");
    if (kind == "leave") {
        row.kind = Kind::Leave;
    } else if (kind == "submenu") {
        row.kind = Kind::Submenu;
        const std::string sub = string_of(r, "submenu");
        if (sub == "brightness") {
            row.submenu = Submenu::Brightness;
        } else if (sub == "controller") {
            row.submenu = Submenu::Controller;
        } else {
            fail(at(member(r, "submenu"), "\"submenu\" is \"brightness\" or \"controller\""));
        }
    } else if (kind == "toggle") {
        row.kind = Kind::Toggle;
    } else if (kind == "list") {
        row.kind = Kind::List;
    } else if (kind == "count") {
        row.kind = Kind::Count;
    } else if (kind == "choice") {
        row.kind = Kind::Choice;
    } else if (kind == "volume") {
        row.kind = Kind::Volume;
    } else {
        fail(at(member(r, "kind"),
                "\"kind\" is leave, submenu, toggle, list, count, choice or volume"));
    }
    if (row.kind != Kind::Leave && row.kind != Kind::Submenu) {
        row.setting = string_of(r, "setting");
    }
    if (const Value* values = r.find("values")) {
        if (!values->is_array()) {
            fail(at(*values, "\"values\" is a list"));
        }
        for (const Value& v : values->array()) {
            OptionsValue value;
            if (v.is_string()) {
                value.text = v.string();
            } else if (v.is_array() && v.array().size() == 2 && v.array()[0].is_string()) {
                value.text = v.array()[0].string();
                value.x = int_value(v.array()[1], -4096, 4096, "a value's x");
            } else {
                fail(at(v, "a value is \"text\" or [\"text\", x]"));
            }
            row.values.push_back(value);
        }
    }
    const auto need_values = [&](std::size_t n) {
        if (row.values.size() < n) {
            fail(at(r, "\"values\" needs at least " + std::to_string(n) + " entries"));
        }
    };
    switch (row.kind) {
        case Kind::Toggle:
            need_values(2);
            if (const Value* language = r.find("language_label")) {
                if (!language->is_string()) {
                    fail(at(*language, "\"language_label\" must be a string"));
                }
                row.language_label = language->string();
            }
            break;
        case Kind::List: {
            const Value& codes = member(r, "codes");
            if (!codes.is_array() || codes.array().empty()) {
                fail(at(codes, "\"codes\" lists the stored values"));
            }
            for (const Value& c : codes.array()) {
                row.codes.push_back(int_value(c, -32768, 32767, "a code"));
            }
            need_values(row.codes.size());
            break;
        }
        case Kind::Count:
            need_values(1);
            break;
        case Kind::Choice:
            row.cycle = int_of(r, "cycle", 1, 64);
            need_values(static_cast<std::size_t>(row.cycle));
            row.align_right_x = int_of(r, "align_right_x", -4096, 4096);
            row.glyph_width = int_of(r, "glyph_width", 0, 64);
            break;
        case Kind::Volume:
            row.bar_y = int_of(r, "bar_y", -4096, 4096);
            break;
        default:
            break;
    }
    row.locked = bool_of(r, "locked", false);
    if (r.find("arrows") != nullptr) {
        row.has_arrows = true;
        arrow_pair_of(r, "arrows", row.arrows);
        arrow_pair_of(r, "arrow_borders", row.arrow_borders);
    }
    return row;
}

OptionsList list_of(const Value& l) {
    reject_unknown(l, {"heading", "heading_pos", "rows_x", "rows_y", "reset_text_layer", "underline_x",
                       "underline_y", "bullet_front", "bullet_back", "mouse_split_x", "rows"});
    OptionsList list;
    list.heading = string_of(l, "heading");
    list.heading_pos = point_of(member(l, "heading_pos"));
    list.rows_x = int_of(l, "rows_x", -4096, 4096);
    list.rows_y = int_of(l, "rows_y", -4096, 4096);
    list.reset_text_layer = bool_of(l, "reset_text_layer", true);
    list.underline_x = int_of(l, "underline_x", -4096, 4096);
    list.underline_y = int_of(l, "underline_y", -4096, 4096);
    points_of(member(l, "bullet_front"), list.bullet_front);
    points_of(member(l, "bullet_back"), list.bullet_back);
    if (l.find("mouse_split_x") != nullptr) {
        list.mouse_split_x = int_of(l, "mouse_split_x", -4096, 4096);
    }
    const Value& rows = member(l, "rows");
    if (!rows.is_array() || rows.array().empty()) {
        fail(at(rows, "\"rows\" must list at least one row"));
    }
    bool locked = false;
    for (const Value& r : rows.array()) {
        list.rows.push_back(row_of(r));
        if (locked && !list.rows.back().locked) {
            fail(at(r, "locked rows come last"));
        }
        locked = list.rows.back().locked;
    }
    if (list.rows.front().locked) {
        fail(at(rows, "the first row cannot be locked"));
    }
    return list;
}

}  // namespace

Result<OptionsLayout> parse_options_layout(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<OptionsLayout>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"tree", "highlight_frames", "row_pitch", "text_layer", "label_length", "value_length",
                          "mouse_row_top", "main", "extra", "volume_bar"});
    OptionsLayout l;
    if (root.find("tree") != nullptr) {
        l.tree = string_of(root, "tree");
    }
    l.highlight_frames = int_of(root, "highlight_frames", 1, 64);
    l.row_pitch = int_of(root, "row_pitch", 1, 256);
    l.text_layer = int_of(root, "text_layer", 0, 255);
    l.label_length = int_of(root, "label_length", 1, 255);
    l.value_length = int_of(root, "value_length", 1, 255);
    l.mouse_row_top = int_of(root, "mouse_row_top", -256, 256);
    l.main = list_of(member(root, "main"));
    l.extra = list_of(member(root, "extra"));
    bool home = false;
    for (const OptionsRow& row : l.main.rows) {
        home = home || row.kind == Kind::Leave;
    }
    if (!home) {
        fail("\"main\" needs a leave row");
    }

    const Value& b = member(root, "volume_bar");
    reject_unknown(b, {"x", "notches", "per_notch", "pitch", "width", "height", "lit", "unlit", "partial",
                       "partial_step", "inner_add", "mouse_x", "mouse_grab_x"});
    VolumeBarLayout& bar = l.volume_bar;
    bar.x = int_of(b, "x", -4096, 4096);
    bar.notches = int_of(b, "notches", 1, 64);
    bar.per_notch = int_of(b, "per_notch", 1, 64);
    bar.pitch = int_of(b, "pitch", 1, 64);
    bar.width = int_of(b, "width", 1, 64);
    bar.height = int_of(b, "height", 1, 64);
    bar.lit = gray_of(b, "lit");
    bar.unlit = gray_of(b, "unlit");
    bar.partial = gray_of(b, "partial");
    bar.partial_step = gray_of(b, "partial_step");
    bar.inner_add = gray_of(b, "inner_add");
    bar.mouse_x = int_of(b, "mouse_x", -4096, 4096);
    bar.mouse_grab_x = int_of(b, "mouse_grab_x", -4096, 4096);
    return Result<OptionsLayout>::ok(std::move(l));
} catch (const std::exception& e) {
    return Result<OptionsLayout>::fail(e.what());
}

Result<OptionsLayout> load_options_layout(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "options.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<OptionsLayout>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<OptionsLayout> parsed = parse_options_layout(text);
    if (!parsed) {
        return Result<OptionsLayout>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
