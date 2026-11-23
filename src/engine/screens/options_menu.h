// SPDX-License-Identifier: GPL-3.0-or-later
// The options screen: the main options list (submenus, toggles, the speaker layout,
// volume bars), the extra options list, and the screen's state machine that moves
// between them and the submenus (brightness, controller). Lists, labels,
// values, positions and widget geometry come from gamedata/menu/options.json; the
// settings behind the rows, input, sound, fades, text and the submenus themselves are
// the game's (OptionsHost). Draw records go to a MenuSink like the other menu screens.

#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/screens/menu_widgets.h"
#include "engine/screens/staff_roll.h"
#include "formats/common/status.h"

namespace sh::screens {

// The screen's state (the game keeps it in its state step 0). The numbers are shared
// with the game's state table; 2, 7 and 10 belonged to the screen-position menu.
enum class OptionsState : std::int32_t {
    EnterMain = 0,
    Main = 1,
    Brightness = 3,
    Controller = 4,
    Leave = 5,
    LeaveMain = 6,
    EnterBrightness = 8,
    EnterController = 9,
    LeaveBrightness = 11,
    LeaveController = 12,
    EnterExtra = 13,
    Extra = 14,
    LeaveExtra = 15,
};

// Left/Right are the stick directions the menus step with; PadLeft/PadRight also take the
// d-pad. Shoulder is any of L1, R1, L2, R2.
enum class MenuButton : std::uint8_t {
    Up, Down, Left, Right, PadLeft, PadRight, Enter, Cancel, Option, Shoulder, L1, R1
};
enum class TextColor : std::uint8_t { White, Red, Gold };
enum class Submenu : std::uint8_t { Brightness, Controller };

struct OptionsValue {
    std::string text;
    std::int32_t x = 0;
};

struct OptionsRow {
    // leave: back to the game. submenu: opens `submenu`. toggle: a zero/non-zero setting.
    // list: one of `codes` (the stored values). count: 0..n-1, n from the host. choice:
    // 0..cycle-1 drawn from `values` right-aligned at `x`. volume: a bar of notches.
    enum class Kind : std::uint8_t { Leave, Submenu, Toggle, List, Count, Choice, Volume };
    Kind kind = Kind::Leave;
    std::string label;
    std::int32_t underline = 0;  // width of the selection underline
    Submenu submenu = Submenu::Brightness;
    std::string setting;
    std::vector<OptionsValue> values;  // by value index
    std::vector<std::int32_t> codes;   // list rows: the stored value of each index
    std::int32_t cycle = 0;            // choice rows
    std::int32_t align_right_x = 0;    // choice rows: the value ends here
    std::int32_t glyph_width = 0;      // choice rows: width per character for the alignment
    std::int32_t bar_y = 0;            // volume rows
    bool locked = false;               // shown only once the extras are unlocked
    std::optional<std::string> language_label;  // the row becomes the language row when the host says so
    bool has_arrows = false;
    MenuPoint arrows[2][3];
    MenuPoint arrow_borders[2][3];
};

struct OptionsList {
    std::string heading;
    MenuPoint heading_pos;
    std::int32_t rows_x = 64;
    std::int32_t rows_y = 56;
    bool reset_text_layer = true;    // after the labels (the main list does, the extra list not)
    std::int32_t underline_x = -121;  // the underline's left end
    std::int32_t underline_y = 58;    // from the row's top to the underline, measured upwards
    MenuPoint bullet_front[4];
    MenuPoint bullet_back[4];
    // A click left of this lowers the value, right of it raises it; without it a click raises.
    std::int32_t mouse_split_x = std::numeric_limits<std::int32_t>::min();
    std::vector<OptionsRow> rows;
};

struct VolumeBarLayout {
    std::int32_t x = 0;
    std::int32_t notches = 16;
    std::int32_t per_notch = 8;  // volume per notch; also the step of Left/Right
    std::int32_t pitch = 6;
    std::int32_t width = 5;
    std::int32_t height = 13;
    std::uint32_t lit = 0;
    std::uint32_t unlit = 0;
    std::uint32_t partial = 0;       // the notch holding the volume: partial + rest * partial_step
    std::uint32_t partial_step = 0;
    std::uint32_t inner_add = 0;     // the inner part of a notch is brighter by this
    std::int32_t mouse_x = 0;        // the first notch's left edge on the screen
    std::int32_t mouse_grab_x = 0;   // a drag starts right of this

    std::int32_t max_volume() const { return notches * per_notch; }
};

struct OptionsLayout {
    // The engine menu (gamedata/menus/*.lua) the screen shows in place of the main list; empty: the
    // original main list. The host draws and drives it (OptionsHost::update_tree).
    std::string tree;
    std::int32_t highlight_frames = 8;  // the underline's slide; input waits until it ends
    std::int32_t row_pitch = 16;
    std::int32_t text_layer = 8;
    std::int32_t label_length = 99;
    std::int32_t value_length = 10;
    std::int32_t mouse_row_top = -3;  // a row's hit box starts this far from its text
    OptionsList main;
    OptionsList extra;
    VolumeBarLayout volume_bar;
};

class OptionsHost {
public:
    virtual ~OptionsHost() = default;

    // Where the screen was opened from.
    virtual bool from_gameplay() = 0;
    virtual bool from_title() = 0;
    virtual bool opened_from_pause() = 0;

    // Input of this frame.
    virtual bool pulsed(MenuButton button) = 0;
    virtual bool clicked(MenuButton button) = 0;
    virtual bool held(MenuButton button) = 0;
    virtual bool mouse_ui_position(std::int32_t& x, std::int32_t& y) = 0;
    virtual bool mouse_moved() = 0;
    virtual bool mouse_left_clicked() = 0;
    virtual bool mouse_left_held() = 0;
    virtual bool mouse_right_clicked() = 0;
    virtual std::int32_t mouse_wheel_step() = 0;
    // Mouse actions become this frame's button presses, so the rows handle them as keys.
    virtual void inject_enter() = 0;
    virtual void inject_cancel() = 0;
    virtual void inject_direction(std::int32_t dir) = 0;
    virtual void show_mouse_cursor() = 0;

    // Settings by the row's name.
    virtual std::int32_t value(const std::string& setting) = 0;
    virtual void set_value(const std::string& setting, std::int32_t value) = 0;
    virtual std::int32_t choice_count(const std::string& setting) = 0;
    virtual std::int32_t volume(const std::string& setting) = 0;
    virtual void set_volume(const std::string& setting, std::int32_t volume) = 0;
    virtual bool extras_unlocked() = 0;

    // The language row (the auto-load row on the title screen's options).
    virtual bool language_row_active() = 0;
    virtual std::int32_t language_count() = 0;
    virtual std::int32_t language_slot() = 0;
    virtual void set_language_slot(std::int32_t slot) = 0;
    virtual std::string language_name(std::int32_t slot) = 0;
    virtual std::int32_t language_name_x(std::int32_t slot) = 0;
    virtual std::int32_t language_name_length(std::int32_t slot) = 0;
    virtual std::int32_t language_arrow_spread(std::int32_t slot) = 0;

    virtual void play_sfx(MenuSfx sfx) = 0;
    virtual void play_sound(MenuSfx sfx) = 0;  // the sound driver's direct call

    // The screen's state and its sub-steps; set_state also clears the sub-steps.
    virtual std::int32_t state() = 0;
    virtual void set_state(std::int32_t state) = 0;
    virtual bool fade_finished() = 0;
    virtual void set_fade(Fade fade) = 0;

    // Entering and leaving the screen.
    virtual void update_bgm_track() = 0;
    virtual void update_play_timer() = 0;
    virtual void prepare_screen() = 0;  // first frame: display sync, background, radio
    virtual void leave_screen() = 0;    // back to the state the screen was opened from

    // One frame of the engine menu that replaces the main list (OptionsLayout::tree); `active` is false
    // while the screen fades out or into a submenu (draw only).
    virtual void update_tree(bool /*active*/) {}

    virtual void load_submenu(Submenu submenu) = 0;
    virtual void wait_for_loads() = 0;
    virtual void open_submenu(Submenu submenu) = 0;
    virtual void update_submenu(Submenu submenu) = 0;

    virtual void draw_background() = 0;
    virtual std::int32_t sine(std::int32_t angle) = 0;
    virtual std::int32_t frame_counter() = 0;

    virtual void text_color(TextColor color) = 0;
    virtual void text_position(std::int32_t x, std::int32_t y) = 0;
    virtual void text_layer(std::int32_t layer) = 0;
    virtual void text_reset_layer() = 0;
    virtual void text_draw(const std::string& text, std::int32_t length) = 0;
};

// The selection underline and bullets of one list: the underline slides from the
// previous row to the selected one over the highlight frames.
class SelectionHighlight {
public:
    // `snap` asks for the slide's ends to be taken now (the mouse moved the selection);
    // it is cleared here.
    void draw(const OptionsList& list, std::int32_t rows, std::int32_t selected, std::int32_t previous,
              std::int32_t width_previous, std::int32_t width_selected, std::int32_t timer, bool& snap,
              std::int32_t row_pitch, OptionsHost& host, const MenuWidgets& widgets, MenuSink& sink);

private:
    MenuPoint from_;
    MenuPoint to_;
};

class OptionsScreen {
public:
    OptionsScreen(const OptionsLayout& layout, const MenuWidgets& widgets);

    // One frame of the options game state.
    void update(OptionsHost& host, MenuSink& sink);

    std::int32_t main_selected() const { return main_.selected; }
    std::int32_t extra_selected() const { return extra_.selected; }
    std::int32_t extra_rows() const { return extra_rows_; }

    // The underline's glide timer and the mouse snap of the list on screen.
    std::int32_t& highlight_timer() { return timer_; }
    bool& highlight_snap() { return snap_; }

private:
    struct ListState {
        std::int32_t selected = 0;
        std::int32_t previous = 0;
        std::vector<std::int32_t> row_data;  // list rows: the index; count rows: the number of values
        SelectionHighlight highlight;
    };

    void enter(OptionsHost& host);
    void list_update(const OptionsList& list, ListState& state, std::int32_t rows, bool main, OptionsHost& host,
                     MenuSink& sink);
    bool advance_timer();

    void draw_labels(const OptionsList& list, std::int32_t rows, OptionsHost& host) const;
    void draw_values(const OptionsList& list, const ListState& state, std::int32_t rows, OptionsHost& host,
                     MenuSink& sink) const;
    void draw_volume_bars(const OptionsList& list, OptionsHost& host, MenuSink& sink) const;

    void mouse(const OptionsList& list, ListState& state, std::int32_t rows, OptionsHost& host);
    void row_input(const OptionsRow& row, std::int32_t& data, OptionsHost& host);
    void volume_input(const OptionsRow& row, OptionsHost& host) const;

    const OptionsLayout& layout_;
    const MenuWidgets& widgets_;
    ListState main_;
    ListState extra_;
    std::int32_t timer_ = 0;
    bool snap_ = false;
    std::int32_t extra_rows_ = 0;
    std::int32_t main_home_ = 0;  // the leave row: selected on entry and by Cancel
};

Result<OptionsLayout> parse_options_layout(std::string_view json_text);
// menu/options.json from the last mod that has it, else from the gamedata directory.
Result<OptionsLayout> load_options_layout(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source = nullptr);

}  // namespace sh::screens
