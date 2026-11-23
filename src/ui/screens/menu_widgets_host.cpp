// SPDX-License-Identifier: GPL-3.0-or-later
// The menus' selection widgets and their sink (menu_widgets_host.h).

#include "ui/screens/menu_widgets_host.h"

#include <cstdint>
#include <optional>
#include <string>

#include "engine/app/pc_paths.h"
#include "engine/log/sh_log.h"
#include "engine/screens/screens_view.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::ui {
namespace {

namespace sc = sh::screens;
namespace d2 = sh::draw2d;

std::optional<sc::MenuWidgets> s_widgets;
std::optional<sc::MenuStyle> s_style;
bool s_load_tried = false;
std::uint32_t s_epoch = sh::mods::kNeverRead;

// The colour words carry the primitive's code in their high byte: a line (0x40) or a polygon (0x20), with the
// rectangle size bits (0x10) the original packs in. Bit 1 of the code is the translucent flag.
constexpr std::uint32_t kLineCode = 0x40 | 0x10;
constexpr std::uint32_t kTriangleCode = 0x20 | 0x10;

std::uint8_t code_of(std::uint32_t rgb, std::uint32_t code) { return static_cast<std::uint8_t>((rgb | (code << 24)) >> 24); }

d2::Rgb rgb_of(std::uint32_t word)
{
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16)};
}

d2::Point point_of(std::int32_t x, std::int32_t y) { return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)}; }

// The order-table place of each widget layer (the original options screen's layers).
struct Place {
    sc::Table2d table;
    int entry;
};
Place place(sc::MenuPrim::Layer layer)
{
    switch (layer) {
        case sc::MenuPrim::Layer::Shadow: return {sc::Table2d::Menu, 9};
        case sc::MenuPrim::Layer::Arrow: return {sc::Table2d::Menu, 10};
        case sc::MenuPrim::Layer::Vignette: return {sc::Table2d::World, 2037};
        case sc::MenuPrim::Layer::Underline:
        case sc::MenuPrim::Layer::Bullet:
        case sc::MenuPrim::Layer::Bar: break;
    }
    return {sc::Table2d::Menu, 6};
}

// Subtractive blending on texture page 0 at `place` (the shadow band's and the vignette's).
void blend(Place at)
{
    constexpr std::uint16_t kSubtractive = 2 << 5;
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(kSubtractive);
    mode.dither = true;
    sc::submit_2d(at.table, at.entry, mode);
}

class Sink final : public sc::MenuSink {
public:
    void prim(const sc::MenuPrim& r) override
    {
        const Place at = place(r.layer);
        switch (r.kind) {
            case sc::MenuPrim::Kind::Line: {
                // Only the first colour's code counts for a line.
                d2::Line line;
                line.gouraud = true;
                line.translucent = (code_of(r.rgb[0], kLineCode) & 2) != 0;
                line.color[0] = rgb_of(r.rgb[0]);
                line.color[1] = rgb_of(r.rgb[1]);
                line.xy[0] = point_of(r.x[0], r.y[0]);
                line.xy[1] = point_of(r.x[1], r.y[1]);
                sc::submit_2d(at.table, at.entry, line);
                break;
            }
            case sc::MenuPrim::Kind::Triangle: {
                d2::Polygon poly;
                poly.count = 3;
                poly.gouraud = true;
                poly.shade.translucent = (code_of(r.rgb[0], kTriangleCode) & 2) != 0;
                // Every colour word carries the code, so the spare bytes the renderer reads as fog hold it too.
                for (int i = 0; i < 3; ++i) {
                    poly.color[i] = rgb_of(r.rgb[i]);
                    poly.fog.amount[i] = code_of(r.rgb[i], kTriangleCode);
                    poly.xy[i] = point_of(r.x[i], r.y[i]);
                }
                sc::submit_2d(at.table, at.entry, poly);
                break;
            }
            case sc::MenuPrim::Kind::Quad: {
                d2::Polygon poly;
                poly.count = 4;
                poly.gouraud = true;
                poly.shade.translucent = r.semi_transparent;
                for (int i = 0; i < 4; ++i) {
                    poly.color[i] = rgb_of(r.rgb[i]);
                    poly.xy[i] = point_of(r.x[i], r.y[i]);
                }
                sc::submit_2d(at.table, at.entry, poly);
                break;
            }
            case sc::MenuPrim::Kind::FlatQuad: {
                d2::Polygon poly;
                poly.count = 4;
                poly.color[0] = rgb_of(r.rgb[0]);
                for (int i = 0; i < 4; ++i) {
                    poly.xy[i] = point_of(r.x[i], r.y[i]);
                }
                sc::submit_2d(at.table, at.entry, poly);
                break;
            }
            case sc::MenuPrim::Kind::Blend:
                // The vignette blends in the world table, everything else at the shadow band's entry.
                blend(r.layer == sc::MenuPrim::Layer::Vignette ? Place{sc::Table2d::World, 2037}
                                                               : Place{sc::Table2d::Menu, 9});
                break;
        }
    }
};

Sink s_sink;

}  // namespace

const sc::MenuWidgets* menu_widgets()
{
    if (sh::mods::data_stale(s_epoch)) {
        s_widgets.reset();
        s_style.reset();
        s_load_tried = false;
    }
    if (s_widgets || s_load_tried) {
        return s_widgets ? &*s_widgets : nullptr;
    }
    s_load_tried = true;
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::string source;
    sh::Result<sc::MenuStyle> style = sc::load_menu_style(PcPort_GetGameDataPath(), mods, &source);
    if (!style) {
        SH_ERR("[MENU] %s - the selection widgets draw nothing", style.error().c_str());
        return nullptr;
    }
    s_style.emplace(style.value());
    s_widgets.emplace(*s_style);
    SH_LOG("[MENU] widget style from %s", source.c_str());
    return &*s_widgets;
}

sc::MenuSink& menu_sink() { return s_sink; }

}  // namespace sh::ui
