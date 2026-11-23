// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hosts of the engine's text and menu drawing over the session and the screens' views: the text
// drawer's host (engine/text/game_text.h: the game font and its characters, the text of the language, the half-height
// glyph flag, the map's messages and their timer, the 2D layer and packet places), the native menu draw layer's host
// (ui/menu/nm_draw_host.h) and the message box's calls into the game (engine/text/message_box_view.h).

#include <cstdint>
#include <string>
#include <variant>

#include "engine/app/game_view.h"
#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/player/player.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"
#include "engine/text/message_box.h"
#include "engine/text/message_box_view.h"
#include "i18n/game_font.h"
#include "i18n/localization.h"
#include "ui/menu/nm_draw_host.h"
#include "ui/screens/menu_widgets_host.h"
#include "render/gpu/render_state.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
std::uint8_t Sd_AudioStreamingCheck(void);
void Player_WeaponModelStateSet(std::int32_t weaponAttack, std::int8_t nextWeaponAttack);
void Gfx_Primitive2dTextureSet(std::int32_t x, std::int32_t y, std::int32_t otIdx, std::int32_t abr);
}

namespace {

namespace t = sh::text;
namespace d2 = sh::draw2d;
namespace sc = sh::screens;
using sh::game::session;

constexpr std::uint8_t kHalfHeightGlyphs = 0x0F;  // the low nibble of the text flags
constexpr int kInterlacedHeight = 448;
// Lines of a map message drawn on one page.
constexpr std::int32_t kMessageLineCap = 9;

bool hi_res_glyphs() { return (session().system.text_flags & kHalfHeightGlyphs) != 0; }
void set_hi_res_glyphs(bool on) {
    std::uint8_t& flags = session().system.text_flags;
    flags = static_cast<std::uint8_t>((flags & ~kHalfHeightGlyphs) | (on ? 1 : 0));
}

// The game's text is UTF-8 and every character comes from the game font (i18n/game_font.h).
class GameChars final : public t::CharSource {
public:
    bool wide_text() override { return true; }
    int map_char(std::uint32_t code, t::GlyphEmit out[2]) override { return sh::i18n::GameFont::get().map(code, out); }
};

// The packet the game's text drawer built for a command.
sc::Packet2d text_packet(const d2::Command& command) {
    if (const auto* s = std::get_if<d2::Sprite>(&command)) {
        return s->size == d2::RectSize::Free ? sc::Packet2d::Sprite : sc::Packet2d::Sprite8;
    }
    if (std::holds_alternative<d2::DrawMode>(command)) {
        return sc::Packet2d::DrawMode;
    }
    return sc::Packet2d::PolyFT4;  // the glyph quads are the only polygons
}

void* advance(void* packet, sc::Packet2d kind) {
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(packet) + sc::packet_size(kind));
}

class TextHost final : public t::GameTextHost {
public:
    t::FontAtlas font() override { return sh::i18n::GameFont::get().atlas(); }
    t::CharSource& chars() override { return chars_; }
    // The language's text for a literal, in the order the drawer draws it (i18n/localization.h).
    const char* menu_text(const char* text) override { return sh::i18n::shown(text); }

    bool hi_res() override { return hi_res_glyphs(); }
    void set_hi_res(bool on) override { set_hi_res_glyphs(on); }

    const char* map_message(std::int32_t index) override { return sh::app::view::map_message(index); }
    std::int32_t message_line_cap() override { return kMessageLineCap; }
    bool right_to_left() override { return sh::i18n::right_to_left(); }
    std::int32_t message_v_shift() override { return sh::render::picture().message_v_shift; }
    float text_size_percent() override { return g_PcConfig.textSize; }
    bool cutscene_active() override { return sh::render::frame_state().cutscene != 0; }
    bool message_timer_running() override { return session().system.map_message_timer != -1; }
    void set_message_timer(std::int32_t value) override { session().system.map_message_timer = value; }
    void set_audio_load_block(std::int32_t block) override {
        t::message_box().selection().audio_load_block = static_cast<std::uint8_t>(block);
    }
    void release_text_layer_exemption() override { sh::render::frame_state().ui2d_bucket = -1; }

    // A run of glyphs (not half-height) builds its packets from where the run began; the shared cursor jumps to the
    // run's end when it closes.
    void begin_run(bool hi_res) override { run_ = hi_res ? nullptr : sc::packet_cursor(); }
    void submit(std::int32_t layer, const d2::Command& command, t::TextStream stream) override {
        const sc::Packet2d kind = text_packet(command);
        if (stream == t::TextStream::Run) {
            sc::link_2d(sc::Table2d::Hud, layer, run_, command);
            run_ = advance(run_, kind);
        } else {
            void* packet = sc::packet_cursor();
            sc::link_2d(sc::Table2d::Hud, layer, packet, command);
            sc::set_packet_cursor(advance(packet, kind));
        }
    }
    void reserve(const d2::Command& command, t::TextStream stream) override {
        const sc::Packet2d kind = text_packet(command);
        if (stream == t::TextStream::Run) {
            run_ = advance(run_, kind);
        } else {
            sc::set_packet_cursor(advance(sc::packet_cursor(), kind));
        }
    }
    void end_run(bool hi_res) override {
        if (!hi_res) {
            sc::set_packet_cursor(run_);
        }
        // Glyphs drawn from another font for the first time reach their texture before the frame is rendered.
        sh::i18n::GameFont::get().flush();
    }

private:
    GameChars chars_;
    void* run_ = nullptr;
};

// The packet the game's menu drawer built for an untextured or textured polygon, a line or a tile.
sc::Packet2d menu_packet(const d2::Command& command) {
    if (const auto* p = std::get_if<d2::Polygon>(&command)) {
        if (p->textured) {
            return p->count == 3 ? sc::Packet2d::PolyFT3 : sc::Packet2d::PolyFT4;
        }
        if (p->gouraud) {
            return p->count == 3 ? sc::Packet2d::PolyG3 : sc::Packet2d::PolyG4;
        }
        return p->count == 3 ? sc::Packet2d::PolyF3 : sc::Packet2d::PolyF4;
    }
    if (const auto* l = std::get_if<d2::Line>(&command)) {
        return l->gouraud ? sc::Packet2d::LineG2 : sc::Packet2d::LineF2;
    }
    return sc::Packet2d::Tile;
}

sh::screens::MenuPoint point(d2::Point p) { return {p.x, p.y}; }

// Pad bits of the left stick's directions.
constexpr std::uint32_t kStickUp = 1u << 24;
constexpr std::uint32_t kStickRight = 1u << 25;
constexpr std::uint32_t kStickDown = 1u << 26;
constexpr std::uint32_t kStickLeft = 1u << 27;

class MenuHost final : public sh::ui::NmDrawHost {
public:
    bool interlaced() override { return session().game.screen_height == kInterlacedHeight; }
    std::int32_t tick() override { return sh::loop::frame_ticks(); }
    std::int32_t frame_counter() override { return session().system.flow.frame_counters[0]; }
    sh::ui::NmPad pad(std::uint32_t exclude) override {
        const sh::game::PadBinds& cfg = session().game.settings.binds;
        const std::uint32_t pulsed = session().game.pads[0].pulsed & ~exclude;
        const std::uint32_t clicked = session().game.pads[0].clicked & ~exclude;
        sh::ui::NmPad p;
        p.up = (pulsed & kStickUp) != 0;
        p.down = (pulsed & kStickDown) != 0;
        p.left = (pulsed & kStickLeft) != 0;
        p.right = (pulsed & kStickRight) != 0;
        p.confirm = (clicked & (cfg.enter | cfg.action)) != 0;
        p.cancel = (clicked & cfg.cancel) != 0;
        return p;
    }
    void set_hi_res_glyphs(bool on) override { ::set_hi_res_glyphs(on); }
    void set_ui_layer(std::int32_t layer) override { sh::render::frame_state().ui2d_bucket = layer; }
    void submit(std::int32_t layer, const d2::Command& command) override {
        sc::submit_2d_packet(sc::Table2d::Menu, layer, command, menu_packet(command));
    }
    void reset_texture_page(std::int32_t layer) override { Gfx_Primitive2dTextureSet(0, 0, layer, 2); }
    void link_scene(std::int32_t layer, std::uint32_t item) override {
        sc::link_scene_2d(sc::Table2d::Menu, layer, item);
    }
    // The options screen's selection widgets (src/ui/screens/menu_widgets_host.h).
    void widget_arrow(const d2::Point tri[3]) override {
        if (const sh::screens::MenuWidgets* w = sh::ui::menu_widgets()) {
            const sh::screens::MenuPoint v[3] = {point(tri[0]), point(tri[1]), point(tri[2])};
            w->arrow(v, true, session().system.flow.frame_counters[0], sh::ui::menu_sink());
        }
    }
    void widget_underline(d2::Point a, d2::Point b, bool shadow) override {
        if (const sh::screens::MenuWidgets* w = sh::ui::menu_widgets()) {
            w->underline(point(a), point(b), shadow, sh::ui::menu_sink());
        }
    }
    void widget_bullet(const d2::Point q[4], bool border, bool dim) override {
        if (const sh::screens::MenuWidgets* w = sh::ui::menu_widgets()) {
            const sh::screens::MenuPoint v[4] = {point(q[0]), point(q[1]), point(q[2]), point(q[3])};
            w->bullet(v, border, dim, sh::ui::menu_sink());
        }
    }
};

}  // namespace

namespace sh::text {

GameText& game_text() {
    static TextHost host;
    static GameText text(host);
    // After `mods reload` the style is read again over the same text state.
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        GameText& t = text;
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        const sh::Result<TextStyle> style = load_text_style(PcPort_GetGameDataPath(), mods, &source);
        if (style) {
            t.set_style(style.value());
            SH_LOG("[TEXT] text style from %s", source.c_str());
        } else {
            SH_ERR("[TEXT] %s - text colours and message placement are all zero", style.error().c_str());
        }
    }
    return text;
}

// The rock drill's tap attack (packed weapon attack: weapon id 2, input kind 0).
constexpr std::int8_t kRockDrillTap = 2;

void stop_player_gas_weapon() {
    sh::game::player().set(sh::game::PlayerVar::GasWeaponTimer, 0);
    Player_WeaponModelStateSet(session().system.combat.weapon_attack, kRockDrillTap);
}

int voice_stream_state() { return Sd_AudioStreamingCheck(); }

void upload_message_font_palette() {
    // The 12x16 font's palette at (304, 510), 16x1 halfwords: the retail disc's words.
    static const std::uint32_t kPalette[] = {0xFFFF0000, 0xBBEEE318, 0xFFEC9304, 0x83FFE30C,
                                             0x001F8318, 0x90840018, 0x90808080, 0x80048084};
    sc::restore_vram(304, 510, 16, 1, reinterpret_cast<const std::uint16_t*>(kPalette));
}

}  // namespace sh::text

namespace sh::ui {

NmDrawHost& nm_draw_host() {
    static MenuHost host;
    return host;
}

}  // namespace sh::ui
