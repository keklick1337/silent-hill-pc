// SPDX-License-Identifier: GPL-3.0-or-later
// The options' brightness screen and gamedata/menu/brightness.json.

#include "engine/screens/brightness_panel.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

// Screen steps (gameStateSteps[1]).
constexpr std::int32_t kStepEnter = 0;
constexpr std::int32_t kStepFadeIn = 1;
constexpr std::int32_t kStepActive = 2;
constexpr std::int32_t kStepLeave = 3;

// The screen coordinate of a percentage, computed in float and truncated.
std::int32_t percent_of(std::int32_t size, float percent) {
    volatile float v = static_cast<float>(size) * (percent / 100.0f);
    return static_cast<std::int32_t>(v);
}

}  // namespace

std::int32_t BrightnessPanel::row_y(BrightnessHost& host, std::int32_t row) const {
    volatile float percent = layout_.row_y + static_cast<float>(row) * layout_.row_pitch;
    return percent_of(host.screen_height(), percent);
}

void BrightnessPanel::adjust(BrightnessHost& host, std::int32_t row, std::int32_t dir) {
    if (row < 0 || row >= static_cast<std::int32_t>(layout_.rows.size())) {
        return;
    }
    const BrightnessRow& r = layout_.rows[row];
    if (r.level) {
        std::int32_t level = host.level(r.setting) + dir;
        const auto lo = static_cast<std::int32_t>(r.min);
        const auto hi = static_cast<std::int32_t>(r.max);
        if (level < lo) level = lo;
        if (level > hi) level = hi;
        if (level == host.level(r.setting)) {
            return;
        }
        host.set_level(r.setting, level);
        host.play_sfx(MenuSfx::Move);
        return;
    }
    if (dir == 0) {
        return;
    }
    volatile float v = host.setting(r.setting) + static_cast<float>(dir) * r.step;
    float value = v;
    if (value < r.min) value = r.min;
    if (value > r.max) value = r.max;
    host.set_setting(r.setting, value);
    host.play_sfx(MenuSfx::Move);
}

void BrightnessPanel::update(BrightnessHost& host, MenuSink& sink) {
    host.show_reference_bar(true);
    const auto rows = static_cast<std::int32_t>(layout_.rows.size());

    switch (host.step(1)) {
        case kStepEnter:
            host.set_step(1, kStepFadeIn);
            host.set_step(2, 0);
            break;

        case kStepFadeIn:
            host.set_fade({FadeState::InStart, false});
            host.set_step(1, kStepActive);
            host.set_step(2, 0);
            break;

        case kStepActive: {
            if (host.pulsed(MenuDir::Up)) {
                row_ = (row_ + rows - 1) % rows;
                host.play_sfx(MenuSfx::Move);
            }
            if (host.pulsed(MenuDir::Down)) {
                row_ = (row_ + 1) % rows;
                host.play_sfx(MenuSfx::Move);
            }
            if (host.pulsed(MenuDir::Left)) {
                adjust(host, row_, -1);
            }
            if (host.pulsed(MenuDir::Right)) {
                adjust(host, row_, +1);
            }
            std::int32_t mx = 0;
            std::int32_t my = 0;
            if (host.mouse_ui_position(mx, my)) {
                const std::int32_t wheel = host.mouse_wheel_step();
                if (wheel != 0) {
                    adjust(host, row_, wheel);
                } else if (host.mouse_left_clicked()) {
                    adjust(host, row_, mx < layout_.mouse_split_x ? -1 : +1);
                }
                if (host.mouse_right_clicked()) {
                    host.inject_cancel();
                }
            }
            const bool enter = host.clicked_enter();
            if (enter || host.clicked_cancel()) {
                host.play_sfx(enter ? MenuSfx::Confirm : MenuSfx::Cancel);
                host.set_fade({FadeState::OutStart, false});
                host.set_step(1, kStepLeave);
                host.set_step(2, 0);
            }
            break;
        }

        case kStepLeave:
            host.show_reference_bar(false);
            if (host.fade().state == FadeState::OutComplete) {
                host.set_fade({FadeState::InStart, false});
                host.leave();
            }
            break;

        default:
            break;
    }

    host.draw_background();
    host.draw_level_lines();
    draw_arrows(host, sink);
    draw_rows(host);
    host.show_mouse_cursor();
}

void BrightnessPanel::draw_arrows(BrightnessHost& host, MenuSink& sink) const {
    const std::int32_t centre = row_y(host, row_) + layout_.arrow_y_offset;
    const auto shape = [&](const MenuPoint (&rel)[3], MenuPoint (&out)[3]) {
        for (int k = 0; k < 3; k++) {
            out[k] = {static_cast<std::int16_t>(rel[k].x), static_cast<std::int16_t>(centre + rel[k].y)};
        }
    };
    const std::int32_t timer = host.frame_counter();
    for (int i = 0; i < 2; i++) {
        MenuPoint v[3];
        shape(layout_.arrows[i], v);
        widgets_.arrow(v, true, timer, sink);
    }
    // The outline marks the side being pushed.
    const int side = host.held(MenuDir::Left) ? 0 : host.held(MenuDir::Right) ? 1 : -1;
    if (side >= 0) {
        MenuPoint v[3];
        shape(layout_.arrow_borders[side], v);
        widgets_.arrow(v, false, timer, sink);
    }
}

void BrightnessPanel::draw_rows(BrightnessHost& host) const {
    const std::int32_t x = percent_of(host.screen_width(), layout_.label_x);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(layout_.rows.size()); i++) {
        const BrightnessRow& r = layout_.rows[i];
        host.draw_label(x, row_y(host, i), i == row_, r.label, layout_.label_length);
        if (r.level) {
            host.draw_number(layout_.value_digits, host.level(r.setting));
        } else {
            volatile float percent = host.live_setting(r.setting) * 100.0f + 0.5f;
            host.draw_number(layout_.value_digits, static_cast<std::int32_t>(percent));
        }
    }
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

float real_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_number()) {
        fail(at(v, "\"" + std::string(key) + "\" must be a number"));
    }
    return static_cast<float>(v.number());
}

std::int32_t int_of(const Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    const Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
                       std::to_string(hi)));
    }
    return static_cast<std::int32_t>(v.integer());
}

void triangle_of(const Value& v, MenuPoint (&out)[3]) {
    if (!v.is_array() || v.array().size() != 3) {
        fail(at(v, "an arrow is three [x, y] points"));
    }
    for (int k = 0; k < 3; k++) {
        const Value& p = v.array()[k];
        if (!p.is_array() || p.array().size() != 2 || !p.array()[0].is_integer() || !p.array()[1].is_integer()) {
            fail(at(p, "a point is [x, y]"));
        }
        out[k] = {static_cast<std::int32_t>(p.array()[0].integer()), static_cast<std::int32_t>(p.array()[1].integer())};
    }
}

void pair_of(const Value& parent, std::string_view key, MenuPoint (&out)[2][3]) {
    const Value& v = member(parent, key);
    if (!v.is_array() || v.array().size() != 2) {
        fail(at(v, "\"" + std::string(key) + "\" lists the left and the right arrow"));
    }
    triangle_of(v.array()[0], out[0]);
    triangle_of(v.array()[1], out[1]);
}

}  // namespace

Result<BrightnessLayout> parse_brightness_layout(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<BrightnessLayout>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"rows", "label_x", "row_y", "row_pitch", "label_length", "value_digits", "arrow_y_offset",
                          "arrows", "arrow_borders", "mouse_split_x"});
    BrightnessLayout l;
    const Value& rows = member(root, "rows");
    if (!rows.is_array() || rows.array().empty()) {
        fail(at(rows, "\"rows\" must list at least one row"));
    }
    for (const Value& r : rows.array()) {
        reject_unknown(r, {"label", "setting", "kind", "min", "max", "step"});
        BrightnessRow row;
        const Value& label = member(r, "label");
        const Value& setting = member(r, "setting");
        const Value& kind = member(r, "kind");
        if (!label.is_string() || !setting.is_string() || !kind.is_string()) {
            fail(at(r, "\"label\", \"setting\" and \"kind\" are strings"));
        }
        if (kind.string() != "percent" && kind.string() != "level") {
            fail(at(kind, "\"kind\" is \"percent\" or \"level\""));
        }
        row.label = label.string();
        row.setting = setting.string();
        row.level = kind.string() == "level";
        row.min = real_of(r, "min");
        row.max = real_of(r, "max");
        row.step = row.level ? 1.0f : real_of(r, "step");
        if (row.max < row.min) {
            fail(at(r, "\"max\" is below \"min\""));
        }
        l.rows.push_back(row);
    }
    l.label_x = real_of(root, "label_x");
    l.row_y = real_of(root, "row_y");
    l.row_pitch = real_of(root, "row_pitch");
    l.label_length = int_of(root, "label_length", 1, 255);
    l.value_digits = int_of(root, "value_digits", 1, 10);
    l.arrow_y_offset = int_of(root, "arrow_y_offset", -1024, 1024);
    pair_of(root, "arrows", l.arrows);
    pair_of(root, "arrow_borders", l.arrow_borders);
    l.mouse_split_x = int_of(root, "mouse_split_x", 0, 4096);
    return Result<BrightnessLayout>::ok(std::move(l));
} catch (const std::exception& e) {
    return Result<BrightnessLayout>::fail(e.what());
}

Result<BrightnessLayout> load_brightness_layout(const std::filesystem::path& gamedata_dir,
                                                const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "brightness.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<BrightnessLayout>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<BrightnessLayout> parsed = parse_brightness_layout(text);
    if (!parsed) {
        return Result<BrightnessLayout>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
