// SPDX-License-Identifier: GPL-3.0-or-later
// The menu selection widgets and gamedata/menu/menu_style.json.

#include "engine/screens/menu_widgets.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

constexpr std::int32_t kFlashPeriodMask = 0x7F;
constexpr std::int32_t kFlashRamp = 32;

void put(MenuPrim& r, int i, MenuPoint p, std::uint32_t rgb) {
    r.x[i] = p.x;
    r.y[i] = p.y;
    r.rgb[i] = rgb;
}

}  // namespace

void MenuWidgets::underline(MenuPoint a, MenuPoint b, bool shadow, MenuSink& sink) const {
    MenuPrim line;
    line.kind = MenuPrim::Kind::Line;
    line.layer = shadow ? MenuPrim::Layer::Shadow : MenuPrim::Layer::Underline;
    put(line, 0, a, style_.underline[0]);
    put(line, 1, b, style_.underline[1]);
    sink.prim(line);
    if (!shadow) {
        return;
    }
    MenuPrim band;
    band.kind = MenuPrim::Kind::Quad;
    band.layer = MenuPrim::Layer::Shadow;
    band.semi_transparent = true;
    put(band, 0, {a.x, a.y - style_.shadow_height}, style_.shadow[0]);
    put(band, 1, a, style_.shadow[1]);
    put(band, 2, {b.x, b.y - style_.shadow_height}, style_.shadow[0]);
    put(band, 3, b, style_.shadow[1]);
    sink.prim(band);
    MenuPrim blend;
    blend.kind = MenuPrim::Kind::Blend;
    blend.layer = MenuPrim::Layer::Shadow;
    sink.prim(blend);
}

void MenuWidgets::arrow(const MenuPoint (&v)[3], bool flashing, std::int32_t timer, MenuSink& sink) const {
    MenuPrim r;
    r.kind = MenuPrim::Kind::Triangle;
    r.layer = MenuPrim::Layer::Arrow;
    if (flashing) {
        // A 128-frame ramp: the tip brightens over 32 frames, then the base follows,
        // both hold, the tip fades, then the base.
        const std::int32_t f = timer & kFlashPeriodMask;
        std::int32_t start;
        std::int32_t end;
        if (f < kFlashRamp) {
            start = f;
            end = 0;
        } else if (f < 2 * kFlashRamp) {
            start = kFlashRamp;
            end = f - kFlashRamp;
        } else if (f < 3 * kFlashRamp) {
            start = 3 * kFlashRamp - f;
            end = kFlashRamp;
        } else {
            start = 0;
            end = 4 * kFlashRamp - f;
        }
        const auto green = [&](std::int32_t level) {
            return style_.arrow_base + (static_cast<std::uint32_t>(level * style_.arrow_flash_step) << 8);
        };
        put(r, 0, v[0], green(end));
        put(r, 1, v[1], green(start));
        put(r, 2, v[2], green(start));
    } else {
        put(r, 0, v[0], style_.arrow_solid);
        put(r, 1, v[1], style_.arrow_solid);
        put(r, 2, v[2], style_.arrow_solid);
    }
    sink.prim(r);
}

void MenuWidgets::bullet(const MenuPoint (&v)[4], bool border, bool inactive, MenuSink& sink) const {
    const std::array<std::uint32_t, 3>& c =
        border ? (inactive ? style_.bullet_border_inactive : style_.bullet_border)
               : (inactive ? style_.bullet_front_inactive : style_.bullet_front);
    // The border splits the quad along 0-3, the front along 1-2.
    static constexpr int kBorder[2][3] = {{0, 2, 3}, {0, 1, 3}};
    static constexpr int kFront[2][3] = {{0, 1, 2}, {3, 1, 2}};
    for (int i = 0; i < 2; i++) {
        const int(&idx)[3] = border ? kBorder[i] : kFront[i];
        MenuPrim r;
        r.kind = MenuPrim::Kind::Triangle;
        r.layer = MenuPrim::Layer::Bullet;
        for (int k = 0; k < 3; k++) {
            put(r, k, v[idx[k]], c[k]);
        }
        sink.prim(r);
    }
}

void MenuWidgets::vignette(MenuSink& sink) const {
    for (std::size_t i = 0; i < style_.vignette_edge_y.size(); i++) {
        MenuPrim r;
        r.kind = MenuPrim::Kind::Quad;
        r.layer = MenuPrim::Layer::Vignette;
        r.semi_transparent = true;
        put(r, 0, {style_.vignette_left, style_.vignette_edge_y[i]}, style_.vignette_edge);
        put(r, 1, {style_.vignette_left, style_.vignette_inner_y[i]}, style_.vignette_inner);
        put(r, 2, {style_.vignette_right, style_.vignette_edge_y[i]}, style_.vignette_edge);
        put(r, 3, {style_.vignette_right, style_.vignette_inner_y[i]}, style_.vignette_inner);
        sink.prim(r);
    }
    MenuPrim blend;
    blend.kind = MenuPrim::Kind::Blend;
    blend.layer = MenuPrim::Layer::Vignette;
    sink.prim(blend);
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

std::uint32_t color_of(const Value& v) {
    if (!v.is_array() || v.array().size() != 3) {
        fail(at(v, "a color is [r, g, b]"));
    }
    std::uint32_t rgb = 0;
    for (std::size_t i = 0; i < 3; i++) {
        const Value& c = v.array()[i];
        if (!c.is_integer() || c.integer() < 0 || c.integer() > 255) {
            fail(at(c, "color components are 0..255"));
        }
        rgb |= static_cast<std::uint32_t>(c.integer()) << (8 * i);
    }
    return rgb;
}

template <std::size_t N>
void colors_of(const Value& parent, std::string_view key, std::array<std::uint32_t, N>& out) {
    const Value& v = member(parent, key);
    if (!v.is_array() || v.array().size() != N) {
        fail(at(v, "\"" + std::string(key) + "\" must list " + std::to_string(N) + " colors"));
    }
    for (std::size_t i = 0; i < N; i++) {
        out[i] = color_of(v.array()[i]);
    }
}

std::int32_t int_of(const Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    const Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
                       std::to_string(hi)));
    }
    return static_cast<std::int32_t>(v.integer());
}

}  // namespace

Result<MenuStyle> parse_menu_style(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<MenuStyle>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"underline", "arrow", "bullet", "vignette"});
    MenuStyle s;

    const Value& ul = member(root, "underline");
    reject_unknown(ul, {"colors", "shadow_height", "shadow_colors"});
    colors_of(ul, "colors", s.underline);
    s.shadow_height = int_of(ul, "shadow_height", 0, 240);
    colors_of(ul, "shadow_colors", s.shadow);

    const Value& ar = member(root, "arrow");
    reject_unknown(ar, {"flash_base", "flash_green_step", "solid"});
    s.arrow_base = color_of(member(ar, "flash_base"));
    s.arrow_flash_step = int_of(ar, "flash_green_step", 0, 7);
    s.arrow_solid = color_of(member(ar, "solid"));

    const Value& bu = member(root, "bullet");
    reject_unknown(bu, {"front", "front_inactive", "border", "border_inactive"});
    colors_of(bu, "front", s.bullet_front);
    colors_of(bu, "front_inactive", s.bullet_front_inactive);
    colors_of(bu, "border", s.bullet_border);
    colors_of(bu, "border_inactive", s.bullet_border_inactive);

    const Value& vi = member(root, "vignette");
    reject_unknown(vi, {"edge_color", "inner_color", "left", "right", "bands"});
    s.vignette_edge = color_of(member(vi, "edge_color"));
    s.vignette_inner = color_of(member(vi, "inner_color"));
    s.vignette_left = int_of(vi, "left", -1024, 1024);
    s.vignette_right = int_of(vi, "right", -1024, 1024);
    const Value& bands = member(vi, "bands");
    if (!bands.is_array() || bands.array().size() != 2) {
        fail(at(bands, "\"bands\" lists the top and the bottom band"));
    }
    for (std::size_t i = 0; i < 2; i++) {
        const Value& b = bands.array()[i];
        reject_unknown(b, {"edge_y", "inner_y"});
        s.vignette_edge_y[i] = int_of(b, "edge_y", -1024, 1024);
        s.vignette_inner_y[i] = int_of(b, "inner_y", -1024, 1024);
    }
    return Result<MenuStyle>::ok(s);
} catch (const std::exception& e) {
    return Result<MenuStyle>::fail(e.what());
}

Result<MenuStyle> load_menu_style(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "menu_style.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<MenuStyle>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<MenuStyle> parsed = parse_menu_style(text);
    if (!parsed) {
        return Result<MenuStyle>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
