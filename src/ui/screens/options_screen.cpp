// SPDX-License-Identifier: GPL-3.0-or-later
// The Options screen on the engine (src/engine/screens/options_menu.h, settings_model.h, brightness_panel.h,
// controls_screen.h): the game's input, settings, sound, fades and text for the screen's state machine, the
// options tree (gamedata/menus/options.lua) shown in place of the original main list, the brightness and
// controller submenus, and the engine menu's natives for the original rows (game.<setting>) and the PC settings
// (PcOpt_*, used by options_binds.cpp). GameState_Options_Update is the game state table's entry.

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/loader/map_list.h"
#include "engine/app/pc_paths.h"
#include "engine/platform/window.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/menu/menu_model.h"
#include "engine/mods/mods_admin.h"
#include "engine/screens/brightness_panel.h"
#include "engine/screens/controls_screen.h"
#include "engine/screens/options_menu.h"
#include "engine/screens/screens_view.h"
#include "engine/screens/settings_model.h"
#include "engine/text/game_text.h"
#include "i18n/localization.h"
#include "input/mouse_pointer.h"
#include "render/gpu/render_state.h"
#include "ui/menu/menu_view.h"
#include "ui/menu/nm_draw.h"
#include "ui/pause/pause_menu.h"
#include "ui/screens/menu_widgets_host.h"
#include "ui/screens/pc_controls_ref.h"
#include "ui/screens/bindings_screen.h"
#include "ui/screens/pc_settings.h"
#include "audio/voice/voice_line_player.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);
extern "C" int TitleMenu_PickOptions(void);
extern unsigned char g_PcTrustedBloodColor;

namespace {

namespace sc = sh::screens;
namespace gs = sh::game_state;
namespace settings = sh::ui::pc_settings;
namespace nm = sh::ui::nm;

// The Aspect Ratio row of the engine menu (setting.resolution_aspect): no config key of its own, it is the shape
// of the current resolution.
constexpr std::string_view kAspectKey = "resolution_aspect";

sh::game::Session& game() { return sh::game::session(); }
sh::game::Settings& game_settings() { return sh::game::session().game.settings; }
sh::game::PadState& pad() { return sh::game::session().game.pads[0]; }

constexpr std::int32_t kVolumeMax = 128;
constexpr int kMenuSfxVolume = 64;
constexpr int kSoundStereo = 2;
constexpr std::int32_t kGameStateOptions = static_cast<std::int32_t>(gs::GameState::OptionScreen);
constexpr std::int32_t kGameStateInGame = static_cast<std::int32_t>(gs::GameState::InGame);
constexpr std::int32_t kGameStateMainMenu = static_cast<std::int32_t>(gs::GameState::MainMenu);

// Pad word bits the menus read besides the directions in sh::game_state::pad.
constexpr std::uint32_t kPadL2 = 1u << 8, kPadR2 = 1u << 9, kPadL1 = 1u << 10, kPadR1 = 1u << 11;

// ---- Loading the screens' data (gamedata/menu/*.json, a mod's copy first) ----------------------------------------

template <typename Layout, typename Loader>
std::optional<Layout> load_layout(Loader loader, const char* what)
{
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::string source;
    sh::Result<Layout> layout = loader(PcPort_GetGameDataPath(), mods, &source);
    if (!layout) {
        SH_ERR("[MENU] %s - %s", layout.error().c_str(), what);
        return std::nullopt;
    }
    SH_LOG("[MENU] %s from %s", what, source.c_str());
    return std::move(layout.value());
}

std::optional<sc::OptionsLayout> s_layout;
std::optional<sc::OptionsScreen> s_screen;
bool s_load_tried = false;
std::optional<sc::SettingsData> s_settings_data;
std::optional<sc::SettingsModel> s_settings;
bool s_settings_load_tried = false;
std::optional<sc::BrightnessLayout> s_brightness_layout;
std::optional<sc::BrightnessPanel> s_brightness;
bool s_brightness_load_tried = false;
std::uint32_t s_epoch = sh::mods::kNeverRead;

// After `mods reload` the screens are built again over the widgets and layouts read again.
void drop_stale()
{
    if (sh::mods::data_stale(s_epoch)) {
        s_screen.reset();
        s_layout.reset();
        s_load_tried = false;
        s_settings.reset();
        s_settings_data.reset();
        s_settings_load_tried = false;
        s_brightness.reset();
        s_brightness_layout.reset();
        s_brightness_load_tried = false;
    }
}

sc::SettingsModel* settings_model()
{
    drop_stale();
    if (s_settings || s_settings_load_tried) {
        return s_settings ? &*s_settings : nullptr;
    }
    s_settings_load_tried = true;
    s_settings_data = load_layout<sc::SettingsData>([](auto&&... a) { return sc::load_settings(a...); }, "settings");
    if (!s_settings_data) {
        return nullptr;
    }
    s_settings.emplace(*s_settings_data);
    return &*s_settings;
}

sc::OptionsScreen* screen()
{
    drop_stale();
    if (s_screen || s_load_tried) {
        return s_screen ? &*s_screen : nullptr;
    }
    const sc::MenuWidgets* widgets = sh::ui::menu_widgets();
    if (widgets == nullptr) {
        return nullptr;
    }
    s_load_tried = true;
    s_layout = load_layout<sc::OptionsLayout>([](auto&&... a) { return sc::load_options_layout(a...); },
                                              "options lists");
    if (!s_layout) {
        return nullptr;
    }
    s_screen.emplace(*s_layout, *widgets);
    return &*s_screen;
}

sc::BrightnessPanel* brightness_panel()
{
    drop_stale();
    if (s_brightness || s_brightness_load_tried) {
        return s_brightness ? &*s_brightness : nullptr;
    }
    const sc::MenuWidgets* widgets = sh::ui::menu_widgets();
    if (widgets == nullptr) {
        return nullptr;
    }
    s_brightness_load_tried = true;
    s_brightness_layout = load_layout<sc::BrightnessLayout>(
        [](auto&&... a) { return sc::load_brightness_layout(a...); }, "brightness rows");
    if (!s_brightness_layout) {
        return nullptr;
    }
    s_brightness.emplace(*s_brightness_layout, *widgets);
    return &*s_brightness;
}

// ---- Shared pieces of the hosts --------------------------------------------------------------------------------

void play_menu_sfx(sc::MenuSfx sfx)
{
    switch (sfx) {
        case sc::MenuSfx::Move: gs::sfx_play(gs::sfx::kMenuMove, kMenuSfxVolume); break;
        case sc::MenuSfx::Confirm: gs::sfx_play(gs::sfx::kMenuConfirm, kMenuSfxVolume); break;
        case sc::MenuSfx::Cancel: gs::sfx_play(gs::sfx::kMenuCancel, kMenuSfxVolume); break;
        case sc::MenuSfx::Error: gs::sfx_play(gs::sfx::kMenuError, kMenuSfxVolume); break;
    }
}

sc::Fade fade_state()
{
    const std::int32_t v = sc::fade_status();
    if (v < 0 || v > 15) {
        return {sc::FadeState::Other, false};
    }
    return {static_cast<sc::FadeState>(v & 7), (v & 8) != 0};
}

void set_fade_state(sc::Fade f) { sc::set_fade_status(static_cast<std::int32_t>(f.state) | (f.white ? 8 : 0)); }

// The screen's state (its step 0); the steps under it and the step's frame counter start over.
void set_screen_state(sc::OptionsState state) { game().set_game_step(0, static_cast<std::int32_t>(state)); }

bool mouse_position(std::int32_t& x, std::int32_t& y)
{
    int mx = 0;
    int my = 0;
    const bool on = sh::input::mouse_pointer().menu_position(&mx, &my) != 0;
    x = mx;
    y = my;
    return on;
}

std::uint32_t button_mask(sc::MenuButton button)
{
    const sh::game::PadBinds& binds = game_settings().binds;
    namespace pd = gs::pad;
    switch (button) {
        case sc::MenuButton::Up: return pd::kStickUp;
        case sc::MenuButton::Down: return pd::kStickDown;
        case sc::MenuButton::Left: return pd::kStickLeft;
        case sc::MenuButton::Right: return pd::kStickRight;
        case sc::MenuButton::PadLeft: return pd::kDpadLeft | pd::kStickLeft;
        case sc::MenuButton::PadRight: return pd::kDpadRight | pd::kStickRight;
        case sc::MenuButton::L1: return kPadL1;
        case sc::MenuButton::R1: return kPadR1;
        case sc::MenuButton::Enter: return binds.enter;
        case sc::MenuButton::Cancel: return binds.cancel;
        case sc::MenuButton::Option: return binds.option;
        case sc::MenuButton::Shoulder: return kPadL2 | kPadR2 | kPadL1 | kPadR1;
    }
    return 0;
}

std::int32_t voice_volume()
{
    const std::int32_t v = static_cast<std::int32_t>((sh::audio::voice_lines().volume() * static_cast<float>(kVolumeMax)) + 0.5f);
    return v < 0 ? 0 : (v > kVolumeMax ? kVolumeMax : v);
}

void warn_setting(const std::string& name)
{
    static bool warned = false;
    if (!warned) {
        warned = true;
        SH_WARN("[MENU] options.json: unknown setting \"%s\"", name.c_str());
    }
}

// The speaker layout ids as config.cfg spells them.
const char* const kSpeakerIds[] = {"auto", "stereo", "quad", "51", "71", "hrtf"};

void run_brightness();
void run_controller();

// ---- The options tree: the engine menu shown in place of the main list -----------------------------------------

// What a row of the tree asked the screen for; the screen acts on it after the menu's step.
enum class TreeRequest { None, Brightness, Leave };
TreeRequest s_tree_request = TreeRequest::None;
bool s_tree_opening = false;
// The console's `options <page>`: the tree's page to show once the screen is up ("-" the first page).
std::string s_console_page;

sh::menu::Controller& tree()
{
    static sh::menu::Controller c(sh::menu::registry());
    return c;
}

sh::menu::MenuView& tree_view()
{
    static sh::menu::MenuView v = [] {
        sh::menu::MenuView view(sh::menu::nm_canvas());
        view.set_style(sh::menu::Style::Options);
        return view;
    }();
    return v;
}

sh::menu::MenuInput tree_input()
{
    sh::menu::MenuInput mi;
    const nm::Input in = nm::read_input();
    mi.up = in.up != 0;
    mi.down = in.down != 0;
    mi.left = in.left != 0;
    mi.right = in.right != 0;
    mi.mirrored = in.mirrored;
    mi.confirm = in.confirm != 0;
    mi.cancel = in.cancel != 0;
    mi.wheel = in.wheel;
    mi.pointer_moved = in.pointer_moved != 0;
    mi.click = in.click != 0;
    mi.right_click = in.right_click != 0;
    if (in.pointer) {
        mi.hover_row = tree_view().hover_row(tree(), in.pointer_x, in.pointer_y);
        mi.hover_answer = tree_view().hover_answer(tree(), in.pointer_x, in.pointer_y);
    }
    return mi;
}

class OptionsHostImpl final : public sc::OptionsHost, public sc::SettingsHost {
public:
    bool from_gameplay() override { return game().game.game_state_previous == kGameStateInGame; }
    bool from_title() override { return game().game.game_state_previous == kGameStateMainMenu; }
    bool opened_from_pause() override { return sh::ui::pause_menu().return_pending(); }

    bool pulsed(sc::MenuButton b) override { return (pad().pulsed & button_mask(b)) != 0; }
    bool clicked(sc::MenuButton b) override { return (pad().clicked & button_mask(b)) != 0; }
    bool held(sc::MenuButton b) override { return (pad().held & button_mask(b)) != 0; }
    bool mouse_ui_position(std::int32_t& x, std::int32_t& y) override { return mouse_position(x, y); }
    bool mouse_moved() override { return sh::input::mouse_pointer().moved() != 0; }
    bool mouse_left_clicked() override { return sh::input::mouse_pointer().left_clicked() != 0; }
    bool mouse_left_held() override { return sh::input::mouse_pointer().left_held() != 0; }
    bool mouse_right_clicked() override { return sh::input::mouse_pointer().right_clicked() != 0; }
    std::int32_t mouse_wheel_step() override { return sh::input::mouse_pointer().wheel_step(); }
    void inject_enter() override { pad().clicked |= game_settings().binds.enter; }
    void inject_cancel() override { pad().clicked |= game_settings().binds.cancel; }
    // Toggle rows read the click, sliders the pulse: a fresh press sets both.
    void inject_direction(std::int32_t dir) override
    {
        const std::uint32_t flag = dir > 0 ? gs::pad::kStickRight : gs::pad::kStickLeft;
        pad().clicked |= flag;
        pad().pulsed |= flag;
    }
    void show_mouse_cursor() override { sh::input::mouse_pointer().show(); }

    std::int32_t value(const std::string& n) override
    {
        const sh::game::Settings& c = game_settings();
        if (n == "vibration") return c.vibration;
        if (n == "auto_load") return c.auto_load;
        if (n == "speaker_layout") return g_PcConfig.audioOutput;
        if (n == "weapon_control") return c.weapon_control_press;
        if (n == "blood_color") return c.blood_color;
        if (n == "view_control") return c.view_reverse;
        if (n == "retreat_turn") return c.retreat_turn_reverse;
        if (n == "walk_run_control") return c.walk_run_reverse;
        if (n == "auto_aiming") return c.auto_aim_off;
        if (n == "view_mode") return c.self_view;
        if (n == "bullet_adjust") return c.bullet_multiplier;
        warn_setting(n);
        return 0;
    }
    void set_value(const std::string& n, std::int32_t v) override
    {
        sh::game::Settings& c = game_settings();
        if (n == "vibration") {
            c.vibration = v != 0 ? 128 : 0;  // the pad's motor level
        } else if (n == "auto_load") {
            c.auto_load = static_cast<std::int8_t>(v);
        } else if (n == "speaker_layout") {
            // The layout is the PC's output; the game's own mix stays stereo so surround layouts keep their image.
            g_PcConfig.audioOutput = v;
            PcConfig_SaveKeyValue("audio_output", kSpeakerIds[v]);
            c.mono = 0;
            gs::sound_command(kSoundStereo);
        } else if (n == "weapon_control") {
            c.weapon_control_press = static_cast<std::uint8_t>(v);
        } else if (n == "blood_color") {
            c.blood_color = static_cast<std::uint8_t>(v);
            // Kept so the effect textures can restore it after a map overwrote it.
            g_PcTrustedBloodColor = c.blood_color;
        } else if (n == "view_control") {
            c.view_reverse = static_cast<std::int8_t>(v);
        } else if (n == "retreat_turn") {
            c.retreat_turn_reverse = static_cast<std::int8_t>(v);
        } else if (n == "walk_run_control") {
            c.walk_run_reverse = static_cast<std::int8_t>(v);
        } else if (n == "auto_aiming") {
            c.auto_aim_off = static_cast<std::int8_t>(v);
        } else if (n == "view_mode") {
            c.self_view = static_cast<std::int8_t>(v);
        } else if (n == "bullet_adjust") {
            c.bullet_multiplier = static_cast<std::int8_t>(v);
        } else {
            warn_setting(n);
        }
    }
    // One bullet multiplier per unlocked extra, plus x1.
    std::int32_t choice_count(const std::string&) override
    {
        std::int32_t n = 1;
        for (int i = 0; i < 5; i++) {
            if (game_settings().extra_options & (1 << i)) {
                n++;
            }
        }
        return n;
    }
    std::int32_t volume(const std::string& n) override
    {
        if (n == "bgm_volume") return game_settings().music_volume;
        if (n == "se_volume") return game_settings().sfx_volume;
        if (n == "voice_volume") return voice_volume();
        warn_setting(n);
        return 0;
    }
    void set_volume(const std::string& n, std::int32_t v) override
    {
        sh::game::Settings& c = game_settings();
        if (n == "bgm_volume") {
            sc::set_sound_volumes(kVolumeMax, v, c.sfx_volume);
            c.music_volume = static_cast<std::uint8_t>(v);
        } else if (n == "se_volume") {
            // The original passes the new value as the music volume here.
            sc::set_sound_volumes(kVolumeMax, v, c.sfx_volume);
            c.sfx_volume = static_cast<std::uint8_t>(v);
        } else if (n == "voice_volume") {
            if (v != voice_volume()) {
                PcConfig_ApplyXaVolume(static_cast<float>(v) / static_cast<float>(kVolumeMax));
            }
        } else {
            warn_setting(n);
        }
    }
    bool extras_unlocked() override { return game_settings().extra_options != 0; }

    // The Language row's arrows in screen x before they are spread, the clear space kept between a name and
    // either arrow, and how far the arrows may move apart (the left one must stay clear of the row's label).
    static constexpr int kLanguageArrowLeft = 208;
    static constexpr int kLanguageArrowRight = 248;
    static constexpr int kLanguageNamePad = 6;
    static constexpr int kLanguageSpreadMin = 26;
    static constexpr int kLanguageSpreadMax = 50;
    static int language_name_width(std::int32_t slot)
    {
        return sh::i18n::text_width(sh::i18n::language_label(slot).c_str());
    }

    // The Language row: every language found in the text directories, by its own name, centred between the
    // row's arrows, which move apart for a long name.
    bool language_row_active() override
    {
        return sh::i18n::languages().size() > 1 && sh::i18n::language_switch_allowed();
    }
    std::int32_t language_count() override { return static_cast<std::int32_t>(sh::i18n::languages().size()); }
    std::int32_t language_slot() override { return sh::i18n::language_index(); }
    void set_language_slot(std::int32_t slot) override { sh::i18n::set_language(slot); }
    std::string language_name(std::int32_t slot) override { return sh::i18n::language_label(slot); }
    std::int32_t language_name_x(std::int32_t slot) override
    {
        return (kLanguageArrowLeft + kLanguageArrowRight) / 2 - language_name_width(slot) / 2;
    }
    std::int32_t language_name_length(std::int32_t slot) override
    {
        return sh::i18n::glyph_count(sh::i18n::language_label(slot).c_str());
    }
    std::int32_t language_arrow_spread(std::int32_t slot) override
    {
        const int need = (language_name_width(slot) + 2 * kLanguageNamePad - (kLanguageArrowRight - kLanguageArrowLeft) + 1) / 2;
        return std::clamp(need, kLanguageSpreadMin, kLanguageSpreadMax);
    }

    void play_sfx(sc::MenuSfx sfx) override { play_menu_sfx(sfx); }
    void play_sound(sc::MenuSfx sfx) override
    {
        switch (sfx) {
            case sc::MenuSfx::Move: gs::sound_command(gs::sfx::kMenuMove); break;
            case sc::MenuSfx::Confirm: gs::sound_command(gs::sfx::kMenuConfirm); break;
            case sc::MenuSfx::Cancel: gs::sound_command(gs::sfx::kMenuCancel); break;
            case sc::MenuSfx::Error: gs::sound_command(gs::sfx::kMenuError); break;
        }
    }

    std::int32_t state() override { return game().game.game_steps[0]; }
    void set_state(std::int32_t s) override { set_screen_state(static_cast<sc::OptionsState>(s)); }
    bool fade_finished() override { return (sc::fade_status() & 7) == static_cast<std::int32_t>(sc::FadeState::OutComplete); }
    void update_tree(bool active) override;
    void set_fade(sc::Fade f) override { set_fade_state(f); }

    void update_bgm_track() override { sc::update_music_track(); }
    void update_play_timer() override { sc::update_play_timer(); }
    void prepare_screen() override
    {
        // Every visit starts at the tree's first page.
        tree().close();
        s_tree_request = TreeRequest::None;
        const bool from_game = game().game.game_state_previous == kGameStateInGame;
        sc::wait_frame_done(!from_game);
        game().game.clear_color.r = 0;
        game().game.clear_color.g = 0;
        game().game.clear_color.b = 0;
        sc::set_frame_interval(1);
        if (from_game) {
            sc::stop_radio_sound();
        }
    }
    // Back to the state the screen was opened from (the two game states swap).
    void leave_screen() override
    {
        game().return_to_previous_screen();
        game().game.game_steps[0] = static_cast<std::int32_t>(sc::OptionsState::EnterMain);
    }

    void load_submenu(sc::Submenu s) override
    {
        if (s == sc::Submenu::Controller) {
            sc::queue_screen_image("TIM/OPTION2.TIM", sc::ScreenImage::ControllerButtons);
        } else if (s == sc::Submenu::Brightness) {
            sc::queue_screen_image("TIM/OP_BRT_E.TIM", from_title() ? sc::ScreenImage::BrightnessFromTitle
                                                                    : sc::ScreenImage::BrightnessFromGame);
        }
    }
    void wait_for_loads() override { sc::wait_for_file_queue(); }
    void open_submenu(sc::Submenu s) override
    {
        if (s == sc::Submenu::Controller) {
            sh::ui::bindings_screen().open();
        }
    }
    void update_submenu(sc::Submenu s) override
    {
        switch (s) {
            case sc::Submenu::Brightness: run_brightness(); break;
            case sc::Submenu::Controller: run_controller(); break;
        }
    }

    void draw_background() override { sc::draw_screen_image(sc::ScreenImage::ItemInspection); }
    std::int32_t sine(std::int32_t angle) override { return gs::sin_q12(angle); }
    std::int32_t frame_counter() override { return game().system.flow.frame_counters[0]; }

    void text_color(sc::TextColor c) override
    {
        switch (c) {
            case sc::TextColor::White: sh::text::game_text().set_color(gs::text_color::kWhite); break;
            case sc::TextColor::Red: sh::text::game_text().set_color(gs::text_color::kRed); break;
            case sc::TextColor::Gold: sh::text::game_text().set_color(gs::text_color::kGold); break;
        }
    }
    void text_position(std::int32_t x, std::int32_t y) override { sh::text::game_text().set_position(x, y); }
    void text_layer(std::int32_t layer) override { sh::text::game_text().set_layer(layer); }
    void text_reset_layer() override { sh::text::game_text().reset_layer(); }
    void text_draw(const std::string& text, std::int32_t length) override
    {
        std::string copy = text;
        sh::text::game_text().draw(copy.data(), length);
    }

    std::int32_t int_setting(const std::string& key) override { return settings::get_int(key); }
    void set_int_setting(const std::string& key, std::int32_t v) override { settings::set_int(key, v); }
    float float_setting(const std::string& key) override { return settings::get_float(key); }
    void set_float_setting(const std::string& key, float v, const std::string* text) override
    {
        settings::set_float(key, v, text);
    }
    std::int32_t default_int(const std::string& key) override { return settings::default_int(key); }
    float default_float(const std::string& key) override { return settings::default_float(key); }
    void window_size(std::int32_t& w, std::int32_t& h) override { settings::window_size(w, h); }
    void set_window_size(std::int32_t w, std::int32_t h) override { settings::set_window_size(w, h); }
    void display_size(std::int32_t& w, std::int32_t& h) override
    {
        int dw = 0, dh = 0;
        sh::platform::display_size(&dw, &dh);
        w = dw;
        h = dh;
    }
    std::string start_map() override { return settings::start_map(); }
    std::int32_t map_count() override { return sh::loader::map_count(); }
    std::int32_t map_index(const std::string& name) override { return sh::loader::find_map(name.c_str()); }
    std::string map_name(std::int32_t i) override { return sh::loader::map_name(i); }
    std::string map_description(std::int32_t i) override
    {
        const char* d = sh::loader::map_description(i);
        return d != nullptr ? d : "";
    }
    void set_start_map(const std::string& name) override { settings::set_start_map(name); }

    // Values stay English: the options view draws them through the game font, whose chokepoint translates English
    // literals (gamedata/text ui.options.value.*); a UTF-8 translation here would reach that font as bytes it
    // cannot show.
    std::string translate(sc::LabelTable, const std::string& text) override { return text; }
    std::string translate_rows(const std::string& count) override
    {
        return sh::i18n::ui_text_filled("{n} rows", "{n}", count.c_str());
    }
    void echo(const std::string& text) override { SH_DBG_ECHO("%s", text.c_str()); }
    void log_view_change(const std::string& key, const std::string& text) override
    {
        settings::log_view_change(key, text);
    }
};

OptionsHostImpl s_host;

void tree_sound(sh::menu::Event e)
{
    using sh::menu::Event;
    switch (e) {
        case Event::Moved:
        case Event::Changed: play_menu_sfx(sc::MenuSfx::Move); break;
        case Event::Activated:
        case Event::Entered: play_menu_sfx(sc::MenuSfx::Confirm); break;
        case Event::Back:
        case Event::Closed: play_menu_sfx(sc::MenuSfx::Cancel); break;
        case Event::Refused: play_menu_sfx(sc::MenuSfx::Error); break;
        case Event::None: break;
    }
}

void OptionsHostImpl::update_tree(bool active)
{
    // Screens a row opened own the frame until they close (no fades: they sit inside the tree).
    if (sh::ui::bindings_screen().is_open()) {
        sh::ui::bindings_screen().frame();
        return;
    }
    if (Pc_ControlsRef_IsOpen()) {
        draw_background();
        Pc_ControlsRef_Update();
        return;
    }
    sh::menu::Controller& c = tree();
    if (!c.is_open()) {
        const std::string& id = s_layout->tree;
        if (!c.open(id)) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                SH_ERR("[MENU] options.json names the menu \"%s\", which gamedata/menus does not define", id.c_str());
            }
            if (active) {
                set_screen_state(sc::OptionsState::Leave);
            }
            return;
        }
        s_tree_opening = true;
    }
    if (!s_console_page.empty() && active) {
        const std::string page = s_console_page;
        s_console_page.clear();
        c.open(s_layout->tree);
        if (page != "-" && !c.enter(page) && !c.enter(s_layout->tree + "." + page)) {
            SH_WARN("[MENU] options: no page \"%s\"", page.c_str());
        }
    }
    sh::menu::MenuView& v = tree_view();
    v.fit(c);
    v.draw(c);
    if (const sc::MenuWidgets* w = sh::ui::menu_widgets()) {
        w->vignette(sh::ui::menu_sink());
    }
    draw_background();
    show_mouse_cursor();
    if (!active) {
        return;
    }

    // The option button leaves straight to the game when the screen was opened from it.
    if (from_gameplay() && !clicked(sc::MenuButton::Enter) && clicked(sc::MenuButton::Option) && !c.confirm_open()) {
        play_menu_sfx(sc::MenuSfx::Cancel);
        set_screen_state(sc::OptionsState::Leave);
        return;
    }
    sh::menu::MenuInput mi = tree_input();
    if (s_tree_opening) {
        // The press that opened the screen and the pointer's resting place are not choices.
        s_tree_opening = false;
        mi = {};
    }
    tree_sound(c.update(mi));

    const TreeRequest req = s_tree_request;
    s_tree_request = TreeRequest::None;
    if (req == TreeRequest::Brightness) {
        load_submenu(sc::Submenu::Brightness);
        set_fade_state({sc::FadeState::OutStart, false});
        set_screen_state(sc::OptionsState::EnterBrightness);
    } else if (req == TreeRequest::Leave || !c.is_open()) {
        set_screen_state(sc::OptionsState::Leave);
    }
}

// ---- The brightness submenu ------------------------------------------------------------------------------------

// The PC output stage's values: the stored config field, the value in effect, the key.
struct OutputSetting {
    const char* name;
    float* config;
    float* live;
};

const OutputSetting* output_setting(const std::string& name)
{
    static const OutputSetting kSettings[] = {
        {"brightness", &g_PcConfig.brightness, &sh::render::graphics().brightness},
        {"contrast", &g_PcConfig.contrast, &sh::render::graphics().contrast},
        {"saturation", &g_PcConfig.saturation, &sh::render::graphics().saturation},
    };
    for (const OutputSetting& s : kSettings) {
        if (name == s.name) {
            return &s;
        }
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        SH_WARN("[MENU] brightness.json: unknown setting \"%s\"", name.c_str());
    }
    return nullptr;
}

std::uint32_t dir_flag(sc::MenuDir dir)
{
    switch (dir) {
        case sc::MenuDir::Up: return gs::pad::kStickUp;
        case sc::MenuDir::Down: return gs::pad::kStickDown;
        case sc::MenuDir::Left: return gs::pad::kStickLeft;
        case sc::MenuDir::Right: break;
    }
    return gs::pad::kStickRight;
}

class BrightnessHostImpl final : public sc::BrightnessHost {
public:
    // The original's fixed 320x240 layout space.
    std::int32_t screen_width() override { return 320; }
    std::int32_t screen_height() override { return 240; }

    bool pulsed(sc::MenuDir dir) override { return (pad().pulsed & dir_flag(dir)) != 0; }
    bool held(sc::MenuDir dir) override { return (pad().held & dir_flag(dir)) != 0; }
    bool clicked_enter() override { return (pad().clicked & game_settings().binds.enter) != 0; }
    bool clicked_cancel() override { return (pad().clicked & game_settings().binds.cancel) != 0; }
    bool mouse_ui_position(std::int32_t& x, std::int32_t& y) override { return mouse_position(x, y); }
    std::int32_t mouse_wheel_step() override { return sh::input::mouse_pointer().wheel_step(); }
    bool mouse_left_clicked() override { return sh::input::mouse_pointer().left_clicked() != 0; }
    bool mouse_right_clicked() override { return sh::input::mouse_pointer().right_clicked() != 0; }
    void inject_cancel() override { pad().clicked |= game_settings().binds.cancel; }
    void show_mouse_cursor() override { sh::input::mouse_pointer().show(); }

    float setting(const std::string& name) override
    {
        const OutputSetting* s = output_setting(name);
        return s != nullptr ? *s->config : 0.0f;
    }
    float live_setting(const std::string& name) override
    {
        const OutputSetting* s = output_setting(name);
        return s != nullptr ? *s->live : 0.0f;
    }
    void set_setting(const std::string& name, float value) override
    {
        const OutputSetting* s = output_setting(name);
        if (s == nullptr) {
            return;
        }
        *s->config = value;
        *s->live = value;
        char text[16];
        std::snprintf(text, sizeof(text), "%.3f", value);
        PcConfig_SaveKeyValue(s->name, text);
    }
    // The original game's brightness level lives in its settings block (saved with it).
    std::int32_t level(const std::string&) override { return game_settings().brightness; }
    void set_level(const std::string&, std::int32_t value) override
    {
        game_settings().brightness = static_cast<std::uint8_t>(value);
    }

    void play_sfx(sc::MenuSfx sfx) override { play_menu_sfx(sfx); }

    std::int32_t step(int level) override { return game().game.game_steps[level]; }
    void set_step(int level, std::int32_t value) override { game().game.game_steps[level] = value; }
    sc::Fade fade() override { return fade_state(); }
    void set_fade(sc::Fade f) override { set_fade_state(f); }
    void leave() override
    {
        set_screen_state(sc::OptionsState::LeaveBrightness);
        game().game.clear_color.r = 0;
        game().game.clear_color.g = 0;
        game().game.clear_color.b = 0;
    }

    void show_reference_bar(bool on) override { sh::render::graphics().calibration_bar = on ? 1 : 0; }
    void draw_background() override
    {
        sc::draw_screen_image(game().game.game_state_previous == kGameStateMainMenu
                                  ? sc::ScreenImage::BrightnessFromTitle
                                  : sc::ScreenImage::BrightnessFromGame);
    }
    void draw_level_lines() override { sc::draw_brightness_lines(game_settings().brightness); }
    void draw_label(std::int32_t x, std::int32_t y, bool selected, const std::string& text,
                    std::int32_t max_length) override
    {
        sh::text::game_text().set_color(selected ? gs::text_color::kGold : gs::text_color::kWhite);
        sh::text::game_text().set_position(x, y);
        std::string copy = text;
        sh::text::game_text().draw(copy.data(), max_length);
    }
    void draw_number(std::int32_t, std::int32_t value) override { sh::text::game_text().draw_int(value); }
    std::int32_t frame_counter() override { return game().system.flow.frame_counters[0]; }
};

BrightnessHostImpl s_brightness_host;

void run_brightness()
{
    if (sc::BrightnessPanel* p = brightness_panel()) {
        p->update(s_brightness_host, sh::ui::menu_sink());
    }
}

// ---- The controller submenu (the bindings screen, src/ui/screens/bindings_screen) --------------------------------

class ControllerHostImpl final : public sc::ControlsScreenHost {
public:
    bool update_screen() override { return sh::ui::bindings_screen().update(); }
    void draw_screen() override { sh::ui::bindings_screen().draw(); }
    void close_screen() override { sh::ui::bindings_screen().close(); }
    void show_mouse_cursor() override { sh::input::mouse_pointer().show(); }
    void consume_menu_input() override
    {
        pad().clicked = 0;
        pad().pulsed = 0;
        pad().pulsed_menu = 0;
    }

    std::int32_t step(int level) override { return game().game.game_steps[level]; }
    void set_step(int level, std::int32_t value) override { game().game.game_steps[level] = value; }
    sc::Fade fade() override { return fade_state(); }
    void set_fade(sc::Fade f) override { set_fade_state(f); }
    void leave() override { set_screen_state(sc::OptionsState::LeaveController); }
};

ControllerHostImpl s_controller_host;
const sc::ControlsScreen s_controller;

void run_controller() { s_controller.update(s_controller_host); }

// ---- The original rows as menu natives (game.<setting>) --------------------------------------------------------

const sc::OptionsRow* game_row(const std::string& setting)
{
    if (screen() == nullptr) {
        return nullptr;
    }
    for (const sc::OptionsList* list : {&s_layout->main, &s_layout->extra}) {
        for (const sc::OptionsRow& row : list->rows) {
            if (row.setting == setting) {
                return &row;
            }
        }
    }
    return nullptr;
}

std::int32_t game_index(const sc::OptionsRow& row)
{
    const std::int32_t v = s_host.value(row.setting);
    switch (row.kind) {
        case sc::OptionsRow::Kind::Toggle: return v != 0 ? 1 : 0;
        case sc::OptionsRow::Kind::List:
            for (std::size_t k = 0; k < row.codes.size(); k++) {
                if (row.codes[k] == v) {
                    return static_cast<std::int32_t>(k);
                }
            }
            return 0;
        default: return v;
    }
}

std::int32_t game_count(const sc::OptionsRow& row)
{
    switch (row.kind) {
        case sc::OptionsRow::Kind::Toggle: return 2;
        case sc::OptionsRow::Kind::List: return static_cast<std::int32_t>(row.codes.size());
        case sc::OptionsRow::Kind::Count: return s_host.choice_count(row.setting);
        case sc::OptionsRow::Kind::Choice: return row.cycle;
        default: return 0;
    }
}

void game_step(const std::string& setting, int dir)
{
    const sc::OptionsRow* row = game_row(setting);
    if (row == nullptr) {
        return;
    }
    if (row->kind == sc::OptionsRow::Kind::Volume) {
        const std::int32_t step = s_layout->volume_bar.per_notch;
        std::int32_t v = s_host.volume(setting) + dir * step;
        const std::int32_t max = s_layout->volume_bar.max_volume();
        v = v < 0 ? 0 : (v > max ? max : v);
        s_host.set_volume(setting, v);
        return;
    }
    const std::int32_t n = game_count(*row);
    if (n <= 0) {
        return;
    }
    const std::int32_t i = ((game_index(*row) + dir) % n + n) % n;
    s_host.set_value(setting, row->kind == sc::OptionsRow::Kind::List ? row->codes[i] : i);
}

// 0..100 with the sign (the game font draws '%').
std::string percent_text(double f)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(f * 100.0 + 0.5));
    return buf;
}

// A slider's value beside its bar: whole numbers for whole steps, else one or two decimals.
std::string slider_text(float v, float step)
{
    char buf[24];
    if (step >= 1.0f) {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
    } else {
        std::snprintf(buf, sizeof(buf), step >= 0.1f - 1e-4f ? "%.1f" : "%.2f", static_cast<double>(v));
    }
    return buf;
}

std::string game_text(const std::string& setting)
{
    const sc::OptionsRow* row = game_row(setting);
    if (row == nullptr) {
        return "";
    }
    if (row->kind == sc::OptionsRow::Kind::Volume) {
        return percent_text(static_cast<double>(s_host.volume(setting)) /
                            static_cast<double>(s_layout->volume_bar.max_volume()));
    }
    const std::int32_t i = game_index(*row);
    if (i >= 0 && i < static_cast<std::int32_t>(row->values.size())) {
        return row->values[i].text;
    }
    return std::to_string(i + 1);
}

// Every text an original row can show (game_text's forms): its value labels, or a volume's ends.
std::vector<std::string> game_texts(const std::string& setting)
{
    std::vector<std::string> out;
    const sc::OptionsRow* row = game_row(setting);
    if (row == nullptr) {
        return out;
    }
    if (row->kind == sc::OptionsRow::Kind::Volume) {
        return {percent_text(0.0), percent_text(1.0)};
    }
    for (const auto& v : row->values) {
        out.push_back(v.text);
    }
    return out;
}

double game_fraction(const std::string& setting)
{
    const sc::OptionsRow* row = game_row(setting);
    if (row == nullptr || row->kind != sc::OptionsRow::Kind::Volume) {
        return -1.0;
    }
    return static_cast<double>(s_host.volume(setting)) / static_cast<double>(s_layout->volume_bar.max_volume());
}

// A setting described by its kind of value, by config key (settings.json "quick").
const sc::QuickSetting* quick_setting(const char* name)
{
    sc::SettingsModel* p = settings_model();
    if (p == nullptr || name == nullptr) {
        return nullptr;
    }
    const sc::QuickSetting* q = p->quick(name);
    if (q == nullptr) {
        static std::string s_warned;
        if (s_warned != name) {
            s_warned = name;
            SH_WARN("[MENU] no setting \"%s\" in settings.json", name);
        }
    }
    return q;
}

// Labels outlive the call: the caller formats several before drawing them.
const char* keep(std::string text)
{
    static std::string s_ring[16];
    static int s_next = 0;
    std::string& slot = s_ring[s_next];
    s_next = (s_next + 1) % 16;
    slot = std::move(text);
    return slot.c_str();
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

extern "C" {

void GameState_Options_Update(void)
{
    if (sc::OptionsScreen* s = screen()) {
        // Every page of the screen (the tree, brightness, controller) uses the whole window, so the picture does
        // not jump between a full width and the 4:3 pillarbox as the pages change.
        sh::menu::claim_full_width();
        s->update(s_host, sh::ui::menu_sink());
    }
}

/* The original options' rows and the options screen's actions as engine menu natives. */
void Pc_OptionsGame_Register(void)
{
    sh::menu::Registry& reg = sh::menu::registry();
    static const char* const kRows[] = {"vibration",   "auto_load",    "voice_volume", "weapon_control",
                                        "blood_color", "view_control", "retreat_turn", "walk_run_control",
                                        "auto_aiming", "view_mode",    "bullet_adjust"};
    for (const char* name : kRows) {
        const std::string setting = name;
        sh::menu::NativeSetting n;
        n.step = [setting](int dir) { game_step(setting, dir); };
        n.text = [setting]() { return game_text(setting); };
        n.fraction = [setting]() { return game_fraction(setting); };
        n.texts = [setting]() { return game_texts(setting); };
        reg.native_setting("game." + setting, std::move(n));
        // Rows the original keeps locked (options.json "locked") show once the extras are unlocked.
        reg.native_visible("game." + setting, [setting]() {
            const sc::OptionsRow* row = game_row(setting);
            return row == nullptr || !row->locked || s_host.extras_unlocked();
        });
    }
    reg.native_action("options.brightness", []() { s_tree_request = TreeRequest::Brightness; });
    reg.native_action("options.exit", []() { s_tree_request = TreeRequest::Leave; });
    // A session in play keeps the mods it started with: the Mods page edits only in Options opened from the title.
    sh::mods::set_edit_gate([]() { return s_host.from_title(); });
}

/* 0..1 for a setting drawn as a bar (sliders, volumes), else -1. */
double PcOpt_SettingFraction(const char* key)
{
    sc::SettingsModel* p = settings_model();
    if (p == nullptr || key == nullptr) {
        return -1.0;
    }
    if (const sc::SettingRow* row = p->find(key)) {
        if (row->kind != sc::SettingRow::Kind::Slider || row->max <= row->min) {
            return -1.0;
        }
        return clamp01((s_host.float_setting(row->setting) - row->min) / (row->max - row->min));
    }
    if (const sc::QuickSetting* q = p->quick(key)) {
        if (q->kind == sc::QuickSetting::Kind::Volume && q->max > 0.0f) {
            return clamp01(static_cast<float>(s_host.int_setting(q->setting)) / q->max);
        }
    }
    return -1.0;
}

/* The text a setting shows in the engine menu: the English engine literal, which the game font's chokepoint
 * translates; "" for an unknown key. */
const char* PcOpt_SettingText(const char* key)
{
    sc::SettingsModel* p = settings_model();
    if (p == nullptr || key == nullptr) {
        return "";
    }
    if (std::string_view(key) == kAspectKey) {
        const std::string label = p->aspect_label(s_host);
        return keep(label.empty() ? std::string("ui.options.value.custom") : label);
    }
    if (const sc::SettingRow* row = p->find(key)) {
        if (row->kind == sc::SettingRow::Kind::Slider) {
            // A 0..1 slider is a level (the movie volume): shown as 0..100 like the other volumes.
            const float v = s_host.float_setting(row->setting);
            if (row->min == 0.0f && row->max == 1.0f) {
                return keep(percent_text(v));
            }
            return keep(slider_text(v, row->step));
        }
        return keep(p->value_label(*row, s_host));
    }
    const sc::QuickSetting* q = quick_setting(key);
    if (q != nullptr && q->kind == sc::QuickSetting::Kind::Volume && q->max > 0.0f) {
        return keep(percent_text(static_cast<double>(s_host.int_setting(q->setting)) / static_cast<double>(q->max)));
    }
    return q != nullptr ? keep(p->quick_label(*q, s_host)) : "";
}

}  // extern "C"

/* Every text a setting can show beside the current one (the labels of its choices, a slider's ends), in the form
 * PcOpt_SettingText gives them: the engine menu sizes the page's value column by the widest. Empty when unknown. */
std::vector<std::string> PcOpt_SettingTexts(const char* key)
{
    std::vector<std::string> out;
    sc::SettingsModel* p = settings_model();
    if (p == nullptr || key == nullptr) {
        return out;
    }
    if (std::string_view(key) == kAspectKey) {
        out = p->aspect_labels();
        out.emplace_back("ui.options.value.custom");
        return out;
    }
    if (const sc::SettingRow* row = p->find(key)) {
        if (row->kind == sc::SettingRow::Kind::Slider) {
            for (const float v : {row->min, row->max}) {
                out.push_back(row->min == 0.0f && row->max == 1.0f ? percent_text(v) : slider_text(v, row->step));
            }
        } else if (row->kind == sc::SettingRow::Kind::Choice) {
            if (row->labels.empty()) {
                for (const std::int32_t v : row->values) out.push_back(std::to_string(v));
            } else {
                out = row->labels;
            }
        }
        return out;
    }
    const sc::QuickSetting* q = p->quick(key);
    if (q == nullptr) {
        return out;
    }
    const auto label = [](const sc::SettingLabel& l) { return s_host.translate(l.table, l.text); };
    switch (q->kind) {
        case sc::QuickSetting::Kind::Toggle:
        case sc::QuickSetting::Kind::Cycle:
            if (q->format == sc::QuickSetting::Format::SizeSquare) {
                for (const std::int32_t v : q->stored) out.push_back(std::to_string(v) + " x " + std::to_string(v));
                break;
            }
            for (const sc::SettingLabel& l : q->labels) out.push_back(label(l));
            if (q->fallback) out.push_back(label(*q->fallback));
            break;
        case sc::QuickSetting::Kind::Volume:
            out.push_back(percent_text(0.0));
            out.push_back(percent_text(1.0));
            break;
        default: break;
    }
    return out;
}

extern "C" {

/* Steps a setting by dir (-1 / +1): applied, clamped and saved to config.cfg by the settings model.
 * Returns 1 when the change only takes effect on the next start. */
int PcOpt_SettingStep(const char* key, int dir)
{
    sc::SettingsModel* p = settings_model();
    if (p == nullptr || key == nullptr) {
        return 0;
    }
    if (std::string_view(key) == kAspectKey) {
        p->adjust_aspect(dir, s_host);
        return 0;
    }
    if (const sc::SettingRow* row = p->find(key)) {
        p->adjust(*row, dir, s_host);
        return row->realtime ? 0 : 1;
    }
    if (const sc::QuickSetting* q = quick_setting(key)) {
        p->quick_adjust(*q, dir, s_host);
    }
    return 0;
}

/* The console's `options [page]`: opens the Options screen from play (as the pause menu's OPTION does) or the title,
 * or shows `page` of the tree when the screen is already up. 0 when none applies. */
int Pc_Options_OpenFromConsole(const char* page)
{
    s_console_page = (page != nullptr && page[0] != '\0') ? page : "-";
    if (game().game.game_state == kGameStateOptions) {
        return 1;
    }
    if (game().game.game_state == kGameStateInGame && game().sys_state() == sh::game::sys_state::kGameplay) {
        game().set_sys_state_next(static_cast<std::int32_t>(gs::SysState::OptionsMenu));
        return 1;
    }
    if (game().game.game_state == kGameStateMainMenu) {
        // The title picks its own OPTION entry: its picture load (OPTION.TIM), fade and leaving (which clears the
        // title's picture area) run as for the player; the screen returns to the title when left.
        if (TitleMenu_PickOptions() != 0) {
            return 1;
        }
    }
    s_console_page.clear();
    return 0;
}

/* Every listed setting back to its default, except the resolution, window mode and map. */
void Pc_Options_ResetToDefaults(void)
{
    if (sc::SettingsModel* p = settings_model()) {
        p->reset_all(s_host);
    }
}

}  // extern "C"
