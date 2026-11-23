// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item screens' 2D over the session: the drawing records of
// sh_inventory (inventory_draw.h, results_info.h) turned into strings and draw2d commands in the menu and world order
// tables (sh::screens::submit_2d_packet keeps every later drawer's packet addresses). Positions, colours and words
// come from the engine and gamedata/menu/inventory_screen.json and results_info.json. The entry points keep the names
// the screen flow, the item views, the save screen, the map events and the text drawer call them by.

#include <cstdint>
#include <string>
#include <utility>

#include "engine/game_state/game_state.h"
#include "engine/app/pc_paths.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/inventory_draw.h"
#include "engine/inventory/item_screen_text.h"
#include "engine/inventory/item_view.h"
#include "engine/inventory/results_info.h"
#include "engine/inventory/screen_data.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/screens/item_screen_style.h"
#include "engine/screens/ranking.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"
#include "i18n/localization.h"
#include "render/draw2d/draw2d.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);


namespace {

namespace inv = sh::inventory;
namespace d2 = sh::draw2d;
using sh::game::save_record;
using sh::game::session;
using sh::screens::Packet2d;
using sh::screens::Table2d;
using sh::text::game_text;

// Selections of the item screens.
constexpr std::uint32_t kSelectionItem = 0, kSelectionEquipped = 1, kSelectionItemCmd = 5, kSelectionEquippedCmd = 6;
constexpr std::int16_t kWhiteText = 7, kDarkGreyText = 1;
constexpr std::int32_t kResultsStepSave = 0x17;  // the results sheet shows before the save step

// Where the item screens' pictures sit in texture memory: the health portrait's palette and the examine picture's.
constexpr std::uint16_t kPortraitPage = 159;
constexpr std::uint16_t kPortraitPalette = (13 << 6) | ((224 >> 4) & 0x3F);
constexpr std::uint16_t kExaminePalette = (14 << 6) | ((224 >> 4) & 0x3F);
// The examine picture sits in two texture pages next to each other.
constexpr std::uint16_t kExaminePages[] = {0x85, 0x86};

template <class C>
d2::Rgb rgb_of(const C& c)
{
    return {static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g), static_cast<std::uint8_t>(c.b)};
}

template <class P>
d2::Point point_of(const P& p)
{
    return {static_cast<std::int16_t>(p.x), static_cast<std::int16_t>(p.y)};
}

template <class P>
d2::TexCoord texel_of(const P& p)
{
    return {static_cast<std::uint8_t>(p.x), static_cast<std::uint8_t>(p.y)};
}

// The packed texture page word of blend mode `abr` at texture memory (x, y).
std::uint16_t texture_page(std::int32_t abr, std::int32_t x, std::int32_t y)
{
    return static_cast<std::uint16_t>(((abr & 0x3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

// The hi-res glyph flag (low nibble of the session's text flags).
void set_hi_res_glyphs(bool on)
{
    std::uint8_t& flags = session().system.text_flags;
    flags = static_cast<std::uint8_t>((flags & 0xF0) | (on ? 1 : 0));
}

// A textured quad at full brightness (colour 128 leaves the texels unchanged).
d2::Polygon picture_quad(std::uint16_t page, std::uint16_t clut)
{
    d2::Polygon poly;
    poly.count = 4;
    poly.textured = true;
    poly.color[0] = {0x80, 0x80, 0x80};
    poly.page = d2::TexturePage::from_word(page);
    poly.palette = d2::Palette::from_word(clut);
    return poly;
}

// A page and blend change in an order table entry.
void texture_mode(Table2d table, std::int32_t entry, std::int32_t abr)
{
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(texture_page(abr, 0, 0));
    mode.dither = true;
    sh::screens::submit_2d_packet(table, entry, mode, Packet2d::DrawMode);
}

// The screen's static layout and words: gamedata/menu/inventory_screen.json (last mod that ships it), the original
// values when it cannot be read.
const inv::InventoryScreenData& screen_data()
{
    static const inv::InventoryScreenData* s_data = nullptr;
    static std::uint32_t s_epoch = sh::mods::kNeverRead;
    if (!sh::mods::data_stale(s_epoch))
        return *s_data;
    s_data = []() -> const inv::InventoryScreenData* {
        static inv::InventoryScreenData loaded;
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<inv::InventoryScreenData> data = inv::load_screen_data(PcPort_GetGameDataPath(), mods, &source);
        if (!data)
        {
            SH_ERR("[INVENTORY] %s - using the built-in screen layout", data.error().c_str());
            return &inv::default_screen_data();
        }
        loaded = std::move(data.value());
        SH_LOG("[INVENTORY] %s", source.c_str());
        return &loaded;
    }();
    return *s_data;
}

const char* label_text(inv::CommandLabel label) { return screen_data().command_words[static_cast<int>(label)].c_str(); }

const char* label_text(inv::ScreenLabel label) { return screen_data().labels[static_cast<int>(label)].text.c_str(); }

// The selection outline and its trail live across frames; preparing the screen resets them.
inv::OutlineTrail s_outline_trail;

// The portrait's scan line and pulse run on across screen openings until the screen resets them.
inv::HealthMonitor s_health_monitor;

class GameRandom final : public inv::HealthRandom {
public:
    std::int32_t next() override { return sh::loop::game_random().next15(); }
    std::int32_t glitch_row() override { return sh::loop::game_random().next_int(0, 133); }
};

void add_gradient(Table2d table, std::int32_t entry, const inv::GradientQuad& quad, bool semi_transparent)
{
    d2::Polygon poly;
    poly.count = 4;
    poly.gouraud = true;
    poly.shade.translucent = semi_transparent;
    for (int i = 0; i < 4; i++)
    {
        poly.color[i] = rgb_of(quad.colors[i]);
        poly.xy[i] = point_of(quad.corners[i]);
    }
    sh::screens::submit_2d_packet(table, entry, poly, Packet2d::PolyG4);
}

void add_line(const inv::LineDraw& line)
{
    d2::Line prim;
    prim.gouraud = true;
    prim.color[0] = rgb_of(line.from_color);
    prim.color[1] = rgb_of(line.to_color);
    prim.xy[0] = point_of(line.from);
    prim.xy[1] = point_of(line.to);
    sh::screens::submit_2d_packet(line.layer == inv::Layer::Overlay ? Table2d::World : Table2d::Menu, 7, prim,
                                  Packet2d::LineG2);
}

// --- The results sheet (gamedata/menu/results_info.json).

std::uint32_t s_results_epoch = sh::mods::kNeverRead;
inv::ResultsInfoData s_results;

const inv::ResultsInfoData* results_data()
{
    if (!sh::mods::data_stale(s_results_epoch))
        return s_results.labels[static_cast<std::size_t>(inv::ResultsText::GameResult)].text.empty() ? nullptr : &s_results;
    s_results = inv::ResultsInfoData{};
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::string source;
    sh::Result<inv::ResultsInfoData> data = inv::load_results_info(PcPort_GetGameDataPath(), mods, &source);
    if (!data)
    {
        SH_ERR("[RESULTS] %s - the results sheet draws nothing", data.error().c_str());
        return nullptr;
    }
    s_results = std::move(data.value());
    SH_LOG("[RESULTS] %s", source.c_str());
    return &s_results;
}

}  // namespace

extern "C" {

void Gfx_Inventory_HealthStatusDraw(void);

void Gfx_Primitive2dTextureSet(std::int32_t x, std::int32_t y, std::int32_t entry, std::int32_t abr)
{
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(texture_page(abr, x, y));
    mode.dither = true;
    // abr 4 and up name the world table (the blend mode keeps only the low two bits).
    sh::screens::submit_2d_packet(abr < 4 ? Table2d::Menu : Table2d::World, entry, mode, Packet2d::DrawMode);
}

void Gfx_ItemScreen_RectDraw(void)
{
    const sh::screens::ItemScreenStyle& style = inv::item_screen_style();
    d2::Polygon poly;
    poly.count = 4;
    poly.color[0] = {static_cast<std::uint8_t>(style.rule_color[0]), static_cast<std::uint8_t>(style.rule_color[1]),
                     static_cast<std::uint8_t>(style.rule_color[2])};
    poly.xy[0] = {static_cast<std::int16_t>(style.rule_x0), static_cast<std::int16_t>(style.rule_y0)};
    poly.xy[1] = {static_cast<std::int16_t>(style.rule_x0), static_cast<std::int16_t>(style.rule_y1)};
    poly.xy[2] = {static_cast<std::int16_t>(style.rule_x1), static_cast<std::int16_t>(style.rule_y0)};
    poly.xy[3] = {static_cast<std::int16_t>(style.rule_x1), static_cast<std::int16_t>(style.rule_y1)};
    sh::screens::submit_2d_packet(Table2d::Menu, 0, poly, Packet2d::PolyF4);
}

void Gfx_Inventory_CmdOptionsDraw(void)
{
    const std::uint32_t selection = inv::inventory_menu().selection;
    const bool active = selection >= kSelectionItemCmd && selection <= kSelectionEquippedCmd;
    game_text().set_color(active ? kWhiteText : kDarkGreyText);

    const std::int32_t idx = (selection != kSelectionEquipped && selection != kSelectionEquippedCmd)
                                 ? static_cast<std::int32_t>(session().system.inventory_selected)
                                 : static_cast<std::int32_t>(session().system.combat.weapon_slot);
    const sh::game::SaveItem& item = save_record().items[idx];
    // The flashlight shows no On/Off while it cannot be used.
    const sh::items::ItemDef* def = inv::game::item_def(item.item);
    const bool on_off_hidden = session().system.environment.flashlight_unavailable && def != nullptr &&
                               def->inventory.switches == "flashlight";

    const inv::CommandColumn column = inv::layout_command_column(screen_data(), item.command, on_off_hidden, active,
                                                                 inv::inventory_menu().command);
    for (int i = 0; i < column.label_count; i++)
    {
        game_text().set_position(column.labels[i].at.x, column.labels[i].at.y);
        game_text().draw(label_text(column.labels[i].label), 10);
    }
    if (!column.active)
        return;

    for (const inv::GradientQuad& quad : column.bar)
        add_gradient(Table2d::Menu, 7, quad, false);
}

void Gfx_Inventory_ScrollArrowsDraw(std::int32_t* selection)
{
    // Only the item row has arrows.
    if (*selection != static_cast<std::int32_t>(kSelectionItem))
        return;

    for (const inv::GradientTriangle& arrow : inv::scroll_arrows(screen_data(), session().system.flow.frame_counters[0]))
    {
        d2::Polygon poly;
        poly.count = 3;
        poly.gouraud = true;
        poly.shade.translucent = true;
        for (int i = 0; i < 3; i++)
        {
            poly.color[i] = rgb_of(arrow.colors[i]);
            poly.xy[i] = point_of(arrow.corners[i]);
        }
        sh::screens::submit_2d_packet(Table2d::Menu, 7, poly, Packet2d::PolyG3);
    }

    Gfx_Primitive2dTextureSet(0, 0, 7, 1);
}

void Gfx_ItemScreens_LabelsDraw(void)
{
    for (const inv::ScreenLabel which : screen_data().label_order)
    {
        const inv::ScreenLabelSpec& spec = screen_data().labels[static_cast<int>(which)];
        const char* english = label_text(which);
        const char* label = english;
        std::int32_t x = spec.at.x;
        if (spec.boxed)
            x += (sh::i18n::text_width(english) - sh::i18n::text_width(sh::i18n::menu_text(label))) / 2;
        game_text().set_position(x, spec.at.y);
        game_text().draw(label, 10);
    }
}

void Gfx_Inventory_HealthStatusReset(void)
{
    s_health_monitor.scan = 32;
    s_health_monitor.pulse = 0;
    s_health_monitor.glitch = 0;
    s_health_monitor.scan_mark = -300;
}

void Gfx_Inventory_HealthStatusDraw(void)
{
    GameRandom random;
    const inv::HealthStatus status =
        inv::layout_health_status(s_health_monitor, session().system.characters.player.health, random);

    for (int i = 0; i < status.strip_count; i++)
    {
        const inv::PortraitStrip& strip = status.strips[i];
        d2::Polygon poly = picture_quad(kPortraitPage, kPortraitPalette);
        for (int j = 0; j < 4; j++)
        {
            poly.uv[j] = texel_of(strip.uv[j]);
            poly.xy[j] = point_of(strip.corners[j]);
        }
        // Each part one ordering slot nearer than the previous one.
        sh::screens::submit_2d_packet(Table2d::World, 2039 - strip.part, poly, Packet2d::PolyFT4);
    }

    add_gradient(Table2d::World, 2036, status.glow, true);
    texture_mode(Table2d::World, 2036, 2);

    for (int i = 0; i < status.bar_count; i++)
        add_gradient(Table2d::World, 2040, status.bars[i], false);
}

void Gfx_Results_Save(void)
{
    // NTSC only: the Japanese build draws the question from a texture instead.
    static const char* const kText[] = {"\x07Is_it_OK_to_save?", "\x07Yes_____________No", "NEXT_GAME_MODE", "NORMAL",
                                        "HARD"};
    const inv::ResultsSave dialog = inv::layout_results_save(static_cast<std::int32_t>(inv::inventory_menu().selection),
                                                             inv::inventory_menu().difficulty);

    for (const inv::FlatLine& line : dialog.lines)
    {
        d2::Line prim;
        prim.color[0] = rgb_of(line.color);
        prim.xy[0] = point_of(line.from);
        prim.xy[1] = point_of(line.to);
        sh::screens::submit_2d_packet(Table2d::Menu, 7, prim, Packet2d::LineF2);
    }

    bool large = false;
    for (int i = 0; i < dialog.label_count; i++)
    {
        const inv::ResultsLabelDraw& label = dialog.labels[i];
        if (label.large != large)
        {
            large = label.large;
            set_hi_res_glyphs(large);
        }
        game_text().set_position(label.at.x, label.at.y);
        game_text().draw(kText[static_cast<int>(label.label)], label.letters);
    }
    if (large)
        set_hi_res_glyphs(false);
}

void Gfx_Inventory_OutlineTrailReset(void)
{
    s_outline_trail = {};
}

void Gfx_Inventory_2dBackgroundDraw(std::int32_t* selection)
{
    Gfx_Inventory_HealthStatusDraw();

    inv::BackgroundInput input;
    input.selection = static_cast<inv::Selection>(*selection);
    input.previous = static_cast<inv::Selection>(inv::inventory_menu().outline_target);
    input.selection_age = inv::inventory_menu().outline_age;
    input.difficulty = save_record().difficulty();
    input.step = session().game.game_steps[1];
    input.frame = session().system.flow.frame_counters[0];
    input.glare_time = inv::item_screen().carousel.glare_time;
    const inv::Background bg = inv::layout_background(s_outline_trail, input);

    for (int i = 0; i < bg.panel_count; i++)
        add_gradient(Table2d::Menu, 7, bg.panels[i], false);

    if (bg.glare)
    {
        const inv::GlareQuad& glare = bg.glare_quad;
        d2::Polygon poly;
        poly.count = 4;
        poly.gouraud = true;
        poly.shade.translucent = true;
        for (int i = 0; i < 4; i++)
        {
            const std::uint8_t grey = static_cast<std::uint8_t>(glare.grey[i]);
            poly.color[i] = {grey, grey, grey};
            poly.xy[i] = point_of(glare.corners[i]);
        }
        sh::screens::submit_2d_packet(Table2d::Menu, 5, poly, Packet2d::PolyG4);

        Gfx_Primitive2dTextureSet(0, 0, 5, 2);

        for (int i = 0; i < bg.picture_count; i++)
        {
            const inv::PictureHalf& half = bg.picture[i];
            d2::Polygon ft4 = picture_quad(kExaminePages[half.page], kExaminePalette);
            for (int j = 0; j < 4; j++)
            {
                ft4.uv[j] = texel_of(half.uv[j]);
                ft4.xy[j] = point_of(half.corners[j]);
            }
            sh::screens::submit_2d_packet(Table2d::Menu, 5, ft4, Packet2d::PolyFT4);
        }
    }

    for (const inv::LineDraw& line : bg.outline)
        add_line(line);
    for (int i = 0; i < bg.corner_count; i++)
        add_line(bg.corners[i]);
}

void Results_DisplayInfo(std::uint32_t* /*unused: the original ignored it*/)
{
    // Only the results step shows the sheet; the later save/restart steps do not.
    if (session().game.game_steps[1] >= kResultsStepSave)
        return;
    const inv::ResultsInfoData* data = results_data();
    if (data == nullptr)
        return;

    // The sheet of the last evaluation, at the widths the game kept its values in.
    const sh::screens::Ranking& r = sh::screens::ranking();
    inv::ResultsInfoInput in;
    in.difficulty = r.difficulty;
    in.saves = r.save_count;
    in.continues = r.continue_count;
    in.hours = r.hours;
    in.minutes = r.minutes;
    in.seconds = r.seconds;
    in.walk_km = r.walk_km;
    in.walk_fraction = r.walk_m;
    in.run_km = r.run_km;
    in.run_fraction = r.run_m;
    in.items = r.items;
    in.special_items = r.special_items;
    in.clears = r.clear_count;
    in.ending_bits = r.current_ending_flags;
    in.rank = r.score;
    in.kills_by_shooting = r.kills_shooting;
    in.kills_by_fighting = r.kills_melee;
    in.fired_shots = r.has_fired_shot;
    for (int i = 0; i < 4; i++)
        in.shot_percent[i] = r.percent_text[i];
    in.color_ending = r.colors.ending;
    in.color_game_clear = r.colors.clear_count;
    in.color_saves = r.colors.save_count;
    in.color_continues = r.colors.continue_count;
    in.color_total_time = r.colors.play_time;
    in.color_items = r.colors.items;
    in.color_special_items = r.colors.special_items;
    in.color_kills = r.colors.enemy_kills;
    in.color_shooting = r.colors.shot_percent;
    const inv::ResultsInfoLayout layout = inv::layout_results_info(*data, in);

    for (const inv::ResultsDraw& draw : layout.draws)
    {
        if (draw.kind == inv::ResultsDrawKind::Number)
        {
            if (draw.positioned)
                game_text().set_position(draw.x, draw.y);
            game_text().set_color(static_cast<std::int16_t>(draw.color));
            game_text().draw_int(draw.number);
            continue;
        }

        const char* text = draw.raw_text;
        if (text == nullptr)
            text = data->labels[static_cast<std::size_t>(draw.key)].text.c_str();
        if (draw.positioned)
        {
            std::int32_t x = draw.x;
            if (draw.align_right)
            {
                // Longer translations keep the word's right edge: shift by the width difference against the English
                // string (unchanged for English).
                x += sh::i18n::text_width(text) - sh::i18n::text_width(sh::i18n::menu_text(text));
            }
            game_text().set_position(x, draw.y);
        }
        game_text().set_color(static_cast<std::int16_t>(draw.color));
        set_hi_res_glyphs(draw.hi_res);
        game_text().draw(text, draw.budget);
    }
    set_hi_res_glyphs(false);

    for (const inv::ResultsQuad& quad : layout.quads)
    {
        d2::Polygon poly;
        poly.count = 4;
        poly.gouraud = true;
        for (int i = 0; i < 4; i++)
        {
            poly.color[i] = {static_cast<std::uint8_t>(quad.colors[i].r), static_cast<std::uint8_t>(quad.colors[i].g),
                             static_cast<std::uint8_t>(quad.colors[i].b)};
            poly.xy[i] = {static_cast<std::int16_t>(quad.corners[i].x), static_cast<std::int16_t>(quad.corners[i].y)};
        }
        sh::screens::submit_2d_packet(Table2d::Menu, 7, poly, Packet2d::PolyG4);
    }
}

}  // extern "C"
