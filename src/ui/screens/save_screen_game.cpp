// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game around the save screen (ui/screens/save_screen_game.h): the host of
// sh::save::game_saves() (map names, script hooks, the blood colour mirror), the two game states that run the screen,
// and the screen's calls into the game (fades, resets, the panels in the menu order table, the state that follows).
// Reads and writes the game only through the session and the engine's views.

#include "ui/screens/save_screen_game.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>

#include <SDL3/SDL_keyboard.h>

#include "engine/app/gameflow/game_flow.h"
#include "engine/mods/save_mods_check.h"
#include "engine/entities/game/remains_host.h"
#include "engine/app/loader/game_files.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/config/pc_config.h"
#include "engine/input/input_actions.h"
#include "engine/input/touch_prompts.h"
#include "engine/loop/frame_view.h"
#include "engine/runtime/game_hooks.h"
#include "engine/save/game_saves.h"
#include "engine/screens/controls_names.h"
#include "engine/screens/save_layout.h"
#include "engine/screens/screens_view.h"
#include "engine/text/message_box_view.h"
#include "i18n/localization.h"
#include "input/glyphs/glyph_sheet.h"
#include "input/mouse_pointer.h"
#include "input/pad.h"
#include "input/touch/touch_controls.h"
#include "render/draw2d/draw2d.h"
#include "engine/app/game_view.h"

extern "C" {
extern unsigned char g_PcTrustedBloodColor;
extern int g_DebugThirdPersonCam;
void Gfx_Primitive2dTextureSet(std::int32_t x, std::int32_t y, std::int32_t otIdx, std::int32_t abr);
}

namespace {

namespace gs = sh::game_state;
namespace d2 = sh::draw2d;
using sh::game::session;
using GameState = gs::GameState;

constexpr std::uint32_t kPadSelect = 1u << 0;
constexpr int kSoundMenuReturn = 23;
constexpr int kWaitEightVblanks = 8;

std::int32_t state(GameState s) { return static_cast<std::int32_t>(s); }

class GameSavesView final : public sh::save::GameSavesHost {
public:
    const char* map_name(int number) override { return gs::map_name(number); }
    int map_number(const char* name) override { return gs::map_find(name); }
    void saving(int number) override { sh::hooks::game_save(number); }
    void loaded(int number) override { sh::hooks::game_load(number); }
    void quick_saving(int index) override { sh::hooks::game_save(index + 1, true); }
    void quick_loaded(int index) override { sh::hooks::game_load(index + 1, true); }
    // Number 0: the save came from a file outside the lists (a state dump).
    void file_loaded() override { sh::hooks::game_load(0, true); }
    std::string remains_text() override { return sh::entities::game::remains::save_text(); }
    std::optional<std::vector<sh::save::SavedMod>> active_mods() override { return sh::mods::active_mods_for_save(); }
    void remains_loaded(const char* text) override {
        sh::entities::game::remains::load_text(text != nullptr ? text : "");
    }
    void blood_color_loaded(std::uint8_t color) override { g_PcTrustedBloodColor = color; }
};

GameSavesView s_view;
[[maybe_unused]] const bool kInstalled = (sh::save::game_saves().set_host(&s_view), true);

// A panel is the original info box (a translucent fill, the upper right and lower left corners cut, a rim of lines
// running from silver to gold), here at any size, in the menu order table.
constexpr int kOtRim = 7;
constexpr int kOtFill = 11;
constexpr int kCut = 8;

std::int16_t screen_x(int x) { return static_cast<std::int16_t>(x - 160); }
std::int16_t screen_y(int y) { return static_cast<std::int16_t>(y - 112); }

std::uint8_t brighter(int c, int glow) {
    c += glow;
    return static_cast<std::uint8_t>(c > 255 ? 255 : c);
}

void rim_draw(int x, int y, int w, int h, int glow) {
    const std::int16_t px[6] = {screen_x(x),     screen_x(x + w - kCut), screen_x(x + w),
                                screen_x(x + w), screen_x(x + kCut),     screen_x(x)};
    const std::int16_t py[6] = {screen_y(y),     screen_y(y),     screen_y(y + kCut),
                                screen_y(y + h), screen_y(y + h), screen_y(y + h - kCut)};
    const std::uint8_t silver = brighter(176, glow);
    const d2::Rgb gold = {brighter(160, glow), brighter(128, glow), brighter(64, glow)};
    const d2::Rgb grey = {silver, silver, silver};
    for (int i = 0; i < 6; i++) {
        const int j = (i + 1) % 6;
        d2::Line line;
        line.gouraud = true;
        line.color[0] = (i & 1) ? gold : grey;
        line.color[1] = (i & 1) ? grey : gold;
        line.xy[0] = {px[i], py[i]};
        line.xy[1] = {px[j], py[j]};
        sh::screens::submit_2d(sh::screens::Table2d::Menu, kOtRim, line);
    }
}

void fill_quad(std::int16_t x0, std::int16_t y0, std::int16_t x1, std::int16_t y1, std::int16_t x2, std::int16_t y2,
               std::int16_t x3, std::int16_t y3) {
    d2::Polygon poly;
    poly.count = 4;
    poly.shade.translucent = true;
    poly.color[0] = {48, 48, 48};
    poly.xy[0] = {x0, y0};
    poly.xy[1] = {x1, y1};
    poly.xy[2] = {x2, y2};
    poly.xy[3] = {x3, y3};
    sh::screens::submit_2d(sh::screens::Table2d::Menu, kOtFill, poly);
}

void clear_color_black() {
    session().game.clear_color.r = 0;
    session().game.clear_color.g = 0;
    session().game.clear_color.b = 0;
}

bool s_delete_key_prev = true;
// A press of the touch Delete the screen did not take within this long is dropped.
constexpr int kTouchDeleteMs = 500;
// The glyph sheet's quads, drawn after the menu text layer (8) as on the controls screen; their origin is the
// middle of the 320x224 text space.
constexpr int kGlyphLayer = 7;
constexpr int kGlyphOriginX = 160;
constexpr int kGlyphOriginY = 112;

// The config's name of the pad button that is the game's Select ("back"), "" when none is bound.
std::string select_button_name() {
    const ControlScheme& scheme = g_DebugThirdPersonCam != 0 ? g_PcConfig.altcam : g_PcConfig.classic;
    for (const char* name : {scheme.padSelect, scheme.padSelect2}) {
        if (!sh::input::is_unbound(name)) return name;
    }
    return {};
}

sh::input::Glyph select_glyph() {
    const std::string name = select_button_name();
    if (name.empty() || !sh::input::glyph_sheet().ready()) return sh::input::Glyph::None;
    const sh::input::Glyph glyph = sh::input::glyph_for_pad(sh::input::glyph_sheet().active_set(), name);
    return glyph == sh::input::Glyph::PadEmpty ? sh::input::Glyph::None : glyph;
}

sh::screens::PadLabels pad_labels(sh::input::GlyphSet set) {
    using sh::input::GlyphSet;
    switch (set) {
        case GlyphSet::PlayStationClassic: return sh::screens::PadLabels::PlayStationClassic;
        case GlyphSet::PlayStation: return sh::screens::PadLabels::PlayStation;
        case GlyphSet::Nintendo: return sh::screens::PadLabels::Nintendo;
        case GlyphSet::Steam: return sh::screens::PadLabels::Steam;
        default: return sh::screens::PadLabels::Xbox;
    }
}
std::string s_load_file;  // a save file the next load screen loads at once (a state dump's); empty: none
int s_quick_load_slot = -1;  // the quick slot the next load screen loads at once; -1: the list

// One tick of the screen; the state's step 0 marks the first tick.
void run_screen(int kind) {
    const int entering = session().game.game_steps[0] == 0;
    session().game.game_steps[0] = 1;
    Pc_SaveScreen_Update(kind, entering);
}

}  // namespace

extern "C" {

void GameState_LoadSavegameScreen_Update(void) {
    if (session().game.game_state == state(GameState::SaveScreen)) {
        run_screen(PcSaveScreen_Save);
        return;
    }
    run_screen(s_quick_load_slot >= 0 || !s_load_file.empty() ? PcSaveScreen_QuickLoad : PcSaveScreen_Load);
}

void GameState_AutoLoadSavegame_Update(void) {
    run_screen(session().game.game_state == state(GameState::AutoLoadSavegame) ? PcSaveScreen_AutoLoad
                                                                                : PcSaveScreen_Load);
}

void PcSaveGame_Enter(int kind) {
    (void)kind;
    if (session().game.game_state_previous == state(GameState::MainMenu)) {
        sh::screens::wait_vblanks(kWaitEightVblanks);
    }
    sh::loop::frame_interval_vblanks() = 1;
    gs::screen_fade_start(true, true, false);
    clear_color_black();
    sh::text::init_message_box();
    session().system.flow.frame_counters[1] = 0;
    s_delete_key_prev = true;
}

void PcSaveGame_BackgroundDraw(int kind) {
    sh::screens::draw_screen_image(sh::screens::ScreenImage::ItemInspection);
    if (kind == PcSaveScreen_Save) {
        sh::screens::update_music_track();
    }
    if (kind != PcSaveScreen_AutoLoad && kind != PcSaveScreen_QuickLoad) {
        sh::input::mouse_pointer().show();
    }
}

void PcSaveGame_PanelDraw(int x, int y, int w, int h) {
    // Three quads that do not overlap, so the translucent fill is even.
    fill_quad(screen_x(x), screen_y(y), screen_x(x + w - kCut), screen_y(y), screen_x(x), screen_y(y + kCut),
              screen_x(x + w), screen_y(y + kCut));
    fill_quad(screen_x(x), screen_y(y + kCut), screen_x(x + w), screen_y(y + kCut), screen_x(x),
              screen_y(y + h - kCut), screen_x(x + w), screen_y(y + h - kCut));
    fill_quad(screen_x(x), screen_y(y + h - kCut), screen_x(x + w), screen_y(y + h - kCut), screen_x(x + kCut),
              screen_y(y + h), screen_x(x + w), screen_y(y + h));
    Gfx_Primitive2dTextureSet(0, 0, kOtFill, 1);
    rim_draw(x, y, w, h, 0);
}

void PcSaveGame_FrameDraw(int x, int y, int w, int h, int glow) { rim_draw(x, y, w, h, glow); }

int PcSaveGame_DeletePressed(void) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool held = keys != nullptr && keys[SDL_SCANCODE_DELETE];
    const bool edge = held && !s_delete_key_prev;
    s_delete_key_prev = held;
    const bool touch = sh::input::take_touch_prompt_press(sh::input::kTouchDelete, kTouchDeleteMs);
    return edge || touch || (session().game.pads[0].clicked & kPadSelect) != 0;
}

void PcSaveGame_AskTouchDelete(void) {
    sh::input::note_touch_prompt(sh::input::kTouchDelete, sh::input::kTouchDelete);
}

int PcSaveGame_HintDevice(void) {
    const bool pad = sh::input::last_device() == sh::input::LastDevice::Pad;
    return static_cast<int>(sh::screens::save_hint_device(pad, sh::input::touch::controls_enabled()));
}

int PcSaveGame_DeleteGlyphWidth(void) {
    sh::input::glyph_sheet().load();
    const sh::input::Glyph glyph = select_glyph();
    return glyph == sh::input::Glyph::None ? 0 : sh::input::glyph_cell(glyph, false).screen_width;
}

void PcSaveGame_DeleteGlyphDraw(int x, int y) {
    const sh::input::Glyph glyph = select_glyph();
    if (glyph == sh::input::Glyph::None) return;
    const int lines = gs::screen_interlaced() ? 2 : 1;
    const sh::input::GlyphCell cell = sh::input::glyph_cell(glyph, true);
    const int tex_w = cell.width;
    const int tex_h = tex_w * sh::input::kGlyphScreenSize / cell.screen_width;
    const int left = x - kGlyphOriginX, right = left + cell.screen_width;
    const int top = (y - kGlyphOriginY) * lines, bottom = (y + sh::input::kGlyphScreenSize - kGlyphOriginY) * lines;
    gs::TexturedQuad quad;
    const int xs[4] = {left, right, left, right};
    const int ys[4] = {top, top, bottom, bottom};
    const int us[4] = {cell.u, cell.u + tex_w, cell.u, cell.u + tex_w};
    const int vs[4] = {cell.v, cell.v, cell.v + tex_h, cell.v + tex_h};
    for (int corner = 0; corner < 4; corner++) {
        quad.x[corner] = static_cast<std::int16_t>(xs[corner]);
        quad.y[corner] = static_cast<std::int16_t>(ys[corner]);
        quad.u[corner] = static_cast<std::uint8_t>(us[corner]);
        quad.v[corner] = static_cast<std::uint8_t>(vs[corner]);
    }
    quad.texture_x = sh::input::kGlyphSheetPageX;
    quad.clut_x = sh::input::kGlyphSheetKeyX;
    quad.clut_y = sh::input::kGlyphSheetKeyY;
    gs::menu_textured_quad(kGlyphLayer, quad);
}

const char* PcSaveGame_DeleteButtonName(void) {
    static std::string text;
    text.clear();
    const std::string name = select_button_name();
    if (name.empty()) return "";
    for (const sh::screens::InputNamePart& part :
         sh::screens::pad_input_name(name, pad_labels(sh::input::glyph_sheet().active_set()))) {
        if (!text.empty()) text += ' ';
        const char* found = part.key.empty() ? nullptr : sh::i18n::ui_text(part.key.c_str());
        text += found != nullptr && part.key != found ? found : part.text;
    }
    return text.c_str();
}

int PcSaveGame_FromBoot(void) { return session().game.game_state_previous == state(GameState::KcetLogo); }

void PcSaveGame_Start(void) {
    session().game.autosave = session().game.save;
    sh::app::view::player_init();
    session().system.process.process_flags = sh::game::process_flag::kLoadSave;
    sh::app::view::map_load(sh::game::save_record().map);
    gs::screen_fade_start(true, false, false);
}

int PcSaveGame_StartDone(void) {
    if (!gs::screen_fade_finished()) {
        return 0;
    }
    sh::loader::queue_wait_empty();
    sh::app::settings_apply_screen_and_volume();
    session().set_game_state_next(state(GameState::MainLoadScreen));
    return 1;
}

void PcSaveGame_Leave(int kind, int boot) {
    (void)kind;
    gs::screen_fade_start(false, false, false);
    if (boot) {
        sh::app::title_graphics_load();
    } else if (session().game.game_state_previous == state(GameState::InventoryScreen)) {
        gs::sound_command(kSoundMenuReturn);
        sh::app::title_graphics_load();
        sh::app::stream_overlay_seek();
    }
}

int PcSaveGame_LeaveDone(int kind) {
    if (!gs::screen_fade_finished()) {
        return 0;
    }
    clear_color_black();
    sh::loader::queue_wait_empty();

    const std::int32_t previous = session().game.game_state_previous;
    // The results screen after the ending opens the save screen from the inventory state and gets it back.
    if (previous == state(GameState::InventoryScreen)) {
        session().set_game_state_next(previous);
    }
    // The load screen opened from gameplay (F8) goes back there, not to a title that never ran its init.
    else if (kind == PcSaveScreen_Save || previous == state(GameState::InGame)) {
        session().set_game_state_next(state(GameState::InGame));
    } else {
        session().set_game_state_next(state(GameState::MainMenu));
    }
    return 1;
}

void PcSaveGame_ToLoadScreen(void) { session().game.game_state = state(GameState::LoadSavegameScreen); }

void PcSaveGame_OpenQuickLoad(int slot) {
    s_quick_load_slot = slot;
    gs::open_load_screen();
}

void PcSaveGame_OpenFileLoad(const char* path) {
    s_load_file = path;
    gs::open_load_screen();
}

const char* PcSaveGame_TakeLoadFile(void) {
    static std::string taken;
    taken = std::move(s_load_file);
    s_load_file.clear();
    return taken.c_str();
}

int PcSaveGame_TakeQuickLoadSlot(void) {
    const int slot = s_quick_load_slot;
    s_quick_load_slot = -1;
    return slot;
}

}  // extern "C"
