// SPDX-License-Identifier: GPL-3.0-or-later
// The options' brightness screen: a panel of rows (the PC output stage's brightness,
// contrast and saturation, and the game's own level 0..7), Up/Down to pick a row,
// Left/Right or the mouse to change it, with flashing arrows beside the selected
// row's value. Rows, labels and layout come from gamedata/menu/brightness.json;
// settings, sound, fades, text and the background are the game's (BrightnessHost).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/screens/menu_widgets.h"
#include "engine/screens/staff_roll.h"
#include "formats/common/status.h"

namespace sh::screens {

struct BrightnessRow {
    std::string label;
    std::string setting;      // the host's name of the value
    bool level = false;       // an integer level 0..max (else a float shown in percent)
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
};

struct BrightnessLayout {
    std::vector<BrightnessRow> rows;
    float label_x = 22.0f;        // percent of the screen width
    float row_y = 40.0f;          // percent of the screen height, first row
    float row_pitch = 8.0f;       // percent per row
    std::int32_t label_length = 20;
    std::int32_t value_digits = 3;
    std::int32_t arrow_y_offset = -107;  // from the row's text y to the arrows' centre
    MenuPoint arrows[2][3];              // left and right arrow, relative to (0, centre)
    MenuPoint arrow_borders[2][3];       // the outline drawn while that side is held
    std::int32_t mouse_split_x = 160;    // a click left of this lowers, right raises
};

class BrightnessHost {
public:
    virtual ~BrightnessHost() = default;

    virtual std::int32_t screen_width() = 0;
    virtual std::int32_t screen_height() = 0;

    // Input of this frame.
    virtual bool pulsed(MenuDir dir) = 0;
    virtual bool held(MenuDir dir) = 0;
    virtual bool clicked_enter() = 0;
    virtual bool clicked_cancel() = 0;
    virtual bool mouse_ui_position(std::int32_t& x, std::int32_t& y) = 0;
    virtual std::int32_t mouse_wheel_step() = 0;
    virtual bool mouse_left_clicked() = 0;
    virtual bool mouse_right_clicked() = 0;
    virtual void inject_cancel() = 0;
    virtual void show_mouse_cursor() = 0;

    // Values by the row's setting name: the stored value, the value in effect.
    virtual float setting(const std::string& name) = 0;
    virtual float live_setting(const std::string& name) = 0;
    virtual void set_setting(const std::string& name, float value) = 0;
    virtual std::int32_t level(const std::string& name) = 0;
    virtual void set_level(const std::string& name, std::int32_t value) = 0;

    virtual void play_sfx(MenuSfx sfx) = 0;

    virtual std::int32_t step(int level) = 0;
    virtual void set_step(int level, std::int32_t value) = 0;
    virtual Fade fade() = 0;
    virtual void set_fade(Fade fade) = 0;
    virtual void leave() = 0;  // back to the main options

    virtual void show_reference_bar(bool on) = 0;
    virtual void draw_background() = 0;
    virtual void draw_level_lines() = 0;  // the reference lines of the game's level
    virtual void draw_label(std::int32_t x, std::int32_t y, bool selected, const std::string& text,
                            std::int32_t max_length) = 0;
    virtual void draw_number(std::int32_t digits, std::int32_t value) = 0;
    virtual std::int32_t frame_counter() = 0;
};

class BrightnessPanel {
public:
    BrightnessPanel(const BrightnessLayout& layout, const MenuWidgets& widgets)
        : layout_(layout), widgets_(widgets) {}

    // One frame of the screen: input and steps, then drawing.
    void update(BrightnessHost& host, MenuSink& sink);

    std::int32_t selected_row() const { return row_; }

private:
    void adjust(BrightnessHost& host, std::int32_t row, std::int32_t dir);
    std::int32_t row_y(BrightnessHost& host, std::int32_t row) const;
    void draw_arrows(BrightnessHost& host, MenuSink& sink) const;
    void draw_rows(BrightnessHost& host) const;

    const BrightnessLayout& layout_;
    const MenuWidgets& widgets_;
    std::int32_t row_ = 0;
};

Result<BrightnessLayout> parse_brightness_layout(std::string_view json_text);
Result<BrightnessLayout> load_brightness_layout(const std::filesystem::path& gamedata_dir,
                                                const std::filesystem::path& mods_dir, std::string* source = nullptr);

}  // namespace sh::screens
