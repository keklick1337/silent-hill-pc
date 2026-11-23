// SPDX-License-Identifier: GPL-3.0-or-later
// Weapon beams and gamedata/effects/weapon_beams.json.

#include "engine/effects/weapon_beams.h"

#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

// The line between two projected points, one pixel thick: the second edge is shifted one
// pixel down and one pixel sideways, away from the side the line leans to.
EffectPrim line_prim(const BeamLineStyle& style, const ScreenPoint& a, const ScreenPoint& b, std::int32_t slot) {
    EffectPrim p;
    p.kind = EffectPrim::Kind::Quad;
    p.semi_transparent = style.semi_transparent;
    p.x[0] = a.x;
    p.y[0] = a.y;
    p.x[1] = b.x;
    p.y[1] = b.y;
    const bool falling = (a.x < b.x && a.y < b.y) || (a.x > b.x && a.y > b.y);
    const int side = falling ? -1 : 1;
    p.x[2] = static_cast<std::int16_t>(a.x + side);
    p.x[3] = static_cast<std::int16_t>(b.x + side);
    p.y[2] = static_cast<std::int16_t>(a.y + 1);
    p.y[3] = static_cast<std::int16_t>(b.y + 1);
    p.u = {style.u_near, style.u_near, style.u_far, style.u_far};
    p.v = {style.v, style.v, style.v, style.v};
    p.texture = style.texture;
    p.texture.palette_y = static_cast<std::uint16_t>(style.texture.palette_y + (slot >> style.palette_slot_shift));
    p.slot = slot;
    return p;
}

}  // namespace

bool WeaponBeams::drawn_on(std::string_view map) const {
    for (const std::string& m : maps_without_beams) {
        if (m == map) {
            return false;
        }
    }
    return true;
}

void draw_hyper_blaster_beam(const HyperBlasterBeamStyle& style, const HyperBlasterShot& shot, BeamHost& host) {
    const Vec3 dir{(((shot.range * host.sin(shot.pitch)) >> 12) * host.sin(shot.yaw)) >> 12,
                   (shot.range * host.cos(shot.pitch)) >> 12,
                   (((shot.range * host.sin(shot.pitch)) >> 12) * host.cos(shot.yaw)) >> 12};
    const BeamHit hit = host.trace(shot.from, dir);
    const std::int32_t segments = hit.hit ? (hit.distance >> 12) + 1 : style.segments_without_hit;
    const std::size_t option = static_cast<std::size_t>(shot.color_option);
    const int sh = style.segment_shift;

    for (std::int32_t i = 0; i < segments; i++) {
        const Vec3 near{shot.from.x + ((dir.x * i) >> sh), shot.from.y + ((dir.y * i) >> sh),
                        shot.from.z + ((dir.z * i) >> sh)};
        const ScreenPoint a = host.project(relative_q8(near, shot.origin));
        const std::int32_t slot = a.depth >> 1;
        if (slot < style.line.slot_min || slot > style.line.slot_max) {
            continue;
        }
        const bool last = i == segments - 1;
        const Vec3 far = last ? hit.target
                              : Vec3{shot.from.x + ((dir.x * (i + 1)) >> sh), shot.from.y + ((dir.y * (i + 1)) >> sh),
                                     shot.from.z + ((dir.z * (i + 1)) >> sh)};
        const ScreenPoint b = host.project(relative_q8(far, shot.origin));
        const std::int32_t far_slot = b.depth >> 1;
        if (far_slot >= style.line.slot_min && far_slot <= style.line.slot_max) {
            EffectPrim p = line_prim(style.line, a, b, slot);
            p.gouraud = true;
            if (option < style.channels.size()) {
                const auto near_level = static_cast<std::uint8_t>(style.intensity_start - style.intensity_step * i);
                const auto far_level = static_cast<std::uint8_t>(style.intensity_start - style.intensity_step * (i + 1));
                const std::array<bool, 3>& c = style.channels[option];
                const Rgb n{c[0] ? near_level : std::uint8_t{0}, c[1] ? near_level : std::uint8_t{0},
                            c[2] ? near_level : std::uint8_t{0}};
                const Rgb f{c[0] ? far_level : std::uint8_t{0}, c[1] ? far_level : std::uint8_t{0},
                            c[2] ? far_level : std::uint8_t{0}};
                p.rgb = {n, f, n, f};
            } else {
                p.has_color = false;
            }
            host.emit(p);
        }
        if (last) {
            EffectPrim tip;
            tip.kind = EffectPrim::Kind::Sprite;
            tip.x[0] = b.x;
            tip.y[0] = b.y;
            tip.w = style.tip_size;
            tip.h = style.tip_size;
            tip.u[0] = style.tip_u;
            tip.v[0] = style.tip_v;
            tip.texture = style.tip_texture;
            tip.slot = slot;
            if (option < style.tip_colors.size()) {
                tip.rgb[0] = style.tip_colors[option];
            } else {
                tip.has_color = false;
            }
            host.emit(tip);
        }
    }
}

void draw_spark_line(const SparkLineStyle& style, const Vec3& from, const Vec3& to, const Vec3& origin,
                     const BeamLights& lights, BeamHost& host) {
    const ScreenPoint a = host.project(relative_q8(from, origin));
    const ScreenPoint b = host.project(relative_q8(to, origin));
    const std::int32_t slot = a.depth >> 1;
    if (slot < style.line.slot_min || slot > style.line.slot_max) {
        return;
    }
    EffectPrim p = line_prim(style.line, a, b, slot);
    p.has_color = false;
    if (lights.map_light_mode >= 0 && static_cast<std::size_t>(lights.map_light_mode) < style.by_light_mode.size()) {
        const SparkLineStyle::ModeColor& mode = style.by_light_mode[static_cast<std::size_t>(lights.map_light_mode)];
        if (mode.kind == SparkLineStyle::ColorKind::Fixed) {
            p.has_color = true;
            p.rgb[0] = mode.rgb;
        } else if (mode.kind == SparkLineStyle::ColorKind::ByFlashlight) {
            // Dim only where the flashlight could light the sparks but is off, and the
            // environment is dark either way.
            bool lit = true;
            if (lights.flashlight_allowed && !lights.flashlight_on) {
                lit = !lights.dark_flashlight_off && !lights.dark_flashlight_on;
            }
            p.has_color = true;
            p.rgb[0] = lit ? style.lit : style.dim;
        }
    }
    host.emit(p);
}

namespace {

using json::Value;

TextureRef texture_of(const Value& parent, std::string_view key) {
    const Value& t = parse::member(parent, key);
    parse::reject_unknown(t, {"page", "palette"});
    TextureRef out;
    out.page = static_cast<std::uint16_t>(parse::integer_of(t, "page", 0, 0xFFFF));
    const Value& pal = parse::member(t, "palette");
    if (!pal.is_array() || pal.array().size() != 2) {
        parse::fail(pal, "\"palette\" is [x, y] in video memory");
    }
    const std::int32_t x = parse::integer(pal.array()[0], 0, 1008);
    if (x % 16 != 0) {
        parse::fail(pal.array()[0], "a palette x is a multiple of 16");
    }
    out.palette_x = static_cast<std::uint16_t>(x);
    out.palette_y = static_cast<std::uint16_t>(parse::integer(pal.array()[1], 0, 511));
    return out;
}

BeamLineStyle line_of(const Value& v) {
    parse::reject_unknown(v, {"texture", "u", "v", "palette_slot_shift", "semi_transparent", "slots"});
    BeamLineStyle out;
    out.texture = texture_of(v, "texture");
    const Value& u = parse::member(v, "u");
    if (!u.is_array() || u.array().size() != 2) {
        parse::fail(u, "\"u\" is [near, far]");
    }
    out.u_near = static_cast<std::uint8_t>(parse::integer(u.array()[0], 0, 255));
    out.u_far = static_cast<std::uint8_t>(parse::integer(u.array()[1], 0, 255));
    out.v = static_cast<std::uint8_t>(parse::integer_of(v, "v", 0, 255));
    out.palette_slot_shift = parse::integer_of(v, "palette_slot_shift", 0, 15);
    out.semi_transparent = parse::boolean_of(v, "semi_transparent");
    const Value& s = parse::member(v, "slots");
    if (!s.is_array() || s.array().size() != 2) {
        parse::fail(s, "\"slots\" is [min, max]");
    }
    out.slot_min = parse::integer(s.array()[0], 0, 4095);
    out.slot_max = parse::integer(s.array()[1], out.slot_min, 4095);
    return out;
}

HyperBlasterBeamStyle hyper_of(const Value& v) {
    parse::reject_unknown(v, {"line", "segments_without_hit", "segment_shift", "intensity_start", "intensity_step",
                              "channels", "tip"});
    HyperBlasterBeamStyle out;
    out.line = line_of(parse::member(v, "line"));
    out.segments_without_hit = parse::integer_of(v, "segments_without_hit", 1, 256);
    out.segment_shift = parse::integer_of(v, "segment_shift", 0, 16);
    out.intensity_start = parse::integer_of(v, "intensity_start", 0, 255);
    out.intensity_step = parse::integer_of(v, "intensity_step", -255, 255);
    const Value& ch = parse::member(v, "channels");
    if (!ch.is_array() || ch.array().empty()) {
        parse::fail(ch, "\"channels\" lists [r, g, b] flags by the beam colour option");
    }
    for (const Value& c : ch.array()) {
        if (!c.is_array() || c.array().size() != 3) {
            parse::fail(c, "channel flags are [r, g, b] of true/false");
        }
        std::array<bool, 3> f{};
        for (std::size_t k = 0; k < 3; k++) {
            if (!c.array()[k].is_bool()) {
                parse::fail(c.array()[k], "true or false expected");
            }
            f[k] = c.array()[k].boolean();
        }
        out.channels.push_back(f);
    }
    const Value& tip = parse::member(v, "tip");
    parse::reject_unknown(tip, {"texture", "uv", "size", "colors"});
    out.tip_texture = texture_of(tip, "texture");
    const Value& uv = parse::member(tip, "uv");
    if (!uv.is_array() || uv.array().size() != 2) {
        parse::fail(uv, "\"uv\" is [u, v]");
    }
    out.tip_u = static_cast<std::uint8_t>(parse::integer(uv.array()[0], 0, 255));
    out.tip_v = static_cast<std::uint8_t>(parse::integer(uv.array()[1], 0, 255));
    out.tip_size = static_cast<std::int16_t>(parse::integer_of(tip, "size", 1, 256));
    const Value& colors = parse::member(tip, "colors");
    if (!colors.is_array()) {
        parse::fail(colors, "\"colors\" lists [r, g, b] by the beam colour option");
    }
    for (const Value& c : colors.array()) {
        std::uint8_t rgb[3];
        parse::rgb(c, rgb);
        out.tip_colors.push_back(Rgb{rgb[0], rgb[1], rgb[2]});
    }
    return out;
}

SparkLineStyle spark_of(const Value& v) {
    parse::reject_unknown(v, {"line", "colors_by_map_light_mode", "lit", "dim"});
    SparkLineStyle out;
    out.line = line_of(parse::member(v, "line"));
    const Value& modes = parse::member(v, "colors_by_map_light_mode");
    if (!modes.is_array()) {
        parse::fail(modes, "\"colors_by_map_light_mode\" is a list");
    }
    for (const Value& m : modes.array()) {
        SparkLineStyle::ModeColor c;
        if (m.is_null()) {
            c.kind = SparkLineStyle::ColorKind::None;
        } else if (m.is_string()) {
            if (m.string() != "by_flashlight") {
                parse::fail(m, "a mode colour is [r, g, b], \"by_flashlight\" or null");
            }
            c.kind = SparkLineStyle::ColorKind::ByFlashlight;
        } else {
            std::uint8_t rgb[3];
            parse::rgb(m, rgb);
            c.kind = SparkLineStyle::ColorKind::Fixed;
            c.rgb = Rgb{rgb[0], rgb[1], rgb[2]};
        }
        out.by_light_mode.push_back(c);
    }
    std::uint8_t rgb[3];
    parse::rgb(parse::member(v, "lit"), rgb);
    out.lit = Rgb{rgb[0], rgb[1], rgb[2]};
    parse::rgb(parse::member(v, "dim"), rgb);
    out.dim = Rgb{rgb[0], rgb[1], rgb[2]};
    return out;
}

}  // namespace

Result<WeaponBeams> parse_weapon_beams(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<WeaponBeams>::fail(doc.error());
    }
    const Value& root = doc.value();
    parse::reject_unknown(root, {"hyper_blaster", "spark_line", "maps_without_beams"});
    WeaponBeams out;
    out.hyper_blaster = hyper_of(parse::member(root, "hyper_blaster"));
    out.spark_line = spark_of(parse::member(root, "spark_line"));
    const Value& maps = parse::member(root, "maps_without_beams");
    if (!maps.is_array()) {
        parse::fail(maps, "\"maps_without_beams\" is a list of map names");
    }
    for (const Value& m : maps.array()) {
        if (!m.is_string() || m.string().empty()) {
            parse::fail(m, "a map name is expected");
        }
        out.maps_without_beams.push_back(m.string());
    }
    return Result<WeaponBeams>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<WeaponBeams>::fail(e.what());
}

Result<WeaponBeams> load_weapon_beams(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                      std::string* source) {
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "weapon_beams.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return Result<WeaponBeams>::fail(text.error());
    }
    Result<WeaponBeams> parsed = parse_weapon_beams(text.value());
    if (!parsed) {
        return Result<WeaponBeams>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
