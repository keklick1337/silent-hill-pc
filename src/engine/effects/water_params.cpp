// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/effects/water.json.

#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/effects/water.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

using json::Value;

const Value& array_of(const Value& parent, std::string_view key, std::size_t size) {
    const Value& v = parse::member(parent, key);
    if (!v.is_array() || (size != 0 && v.array().size() != size)) {
        parse::fail(v, size != 0 ? "a list of " + std::to_string(size) + " is expected" : "a list is expected");
    }
    return v;
}

double real_of(const Value& parent, std::string_view key, double lo, double hi) {
    const Value& v = parse::member(parent, key);
    if (!v.is_number() || v.number() < lo || v.number() > hi) {
        parse::fail(v, "a number from " + std::to_string(lo) + " to " + std::to_string(hi) + " is expected");
    }
    return v.number();
}

WaterCurve curve_of(const Value& root, std::string_view key) {
    const Value& c = parse::member(root, key);
    parse::reject_unknown(c, {"values", "range"});
    WaterCurve out;
    const Value& values = array_of(c, "values", 0);
    if (values.array().size() < 2) {
        parse::fail(values, "a curve needs at least two values");
    }
    for (const Value& e : values.array()) {
        out.values.push_back(parse::integer(e, -(1 << 24), 1 << 24));
    }
    const Value& range = array_of(c, "range", 2);
    out.min_x = parse::integer(range.array()[0], -(1 << 24), 1 << 24);
    out.max_x = parse::integer(range.array()[1], -(1 << 24), 1 << 24);
    if (out.max_x <= out.min_x || (out.max_x - out.min_x) < static_cast<std::int32_t>(out.values.size() - 1)) {
        parse::fail(range, "the range is too short for the values");
    }
    return out;
}

}  // namespace

Result<WaterParams> parse_water_params(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<WaterParams>::fail(doc.error());
    }
    const Value& root = doc.value();
    if (!root.is_object()) {
        throw std::runtime_error("the file must be an object");
    }
    parse::reject_unknown(root, {"flare_intensity", "flare_distance", "flare_angle", "flare_max", "ray_angles",
                                 "ray_shape", "hidden_depth", "glint_slot", "octagon", "scroll", "surface"});
    WaterParams p;
    p.flare_intensity = curve_of(root, "flare_intensity");
    p.flare_distance = curve_of(root, "flare_distance");
    p.flare_angle = curve_of(root, "flare_angle");
    p.flare_max = parse::integer_of(root, "flare_max", 0, 1 << 20);
    const Value& angles = array_of(root, "ray_angles", p.ray_angles.size());
    for (std::size_t i = 0; i < p.ray_angles.size(); i++) {
        p.ray_angles[i] = parse::integer(angles.array()[i], -32768, 32767);
    }
    const Value& shape = array_of(root, "ray_shape", 4);
    for (std::size_t i = 0; i < 4; i++) {
        const Value& pt = shape.array()[i];
        if (!pt.is_array() || pt.array().size() != 2) {
            parse::fail(pt, "a point is [x, y]");
        }
        p.ray_shape[i] = {parse::integer(pt.array()[0], -4096, 4096), parse::integer(pt.array()[1], -4096, 4096)};
    }
    p.hidden_depth = parse::integer_of(root, "hidden_depth", 0, 1 << 20);
    p.glint_slot = parse::integer_of(root, "glint_slot", 0, 4095);

    const Value& oct = parse::member(root, "octagon");
    parse::reject_unknown(oct, {"centre_z", "inner_radius", "inner_z", "outer_radius", "outer_z", "outer_top",
                                "ring_slot", "band_slot", "colors"});
    p.octagon_centre_z = parse::integer_of(oct, "centre_z", -32768, 32767);
    p.inner_radius = parse::integer_of(oct, "inner_radius", 0, 32767);
    p.inner_z = parse::integer_of(oct, "inner_z", -32768, 32767);
    p.outer_radius = parse::integer_of(oct, "outer_radius", 0, 32767);
    p.outer_z = parse::integer_of(oct, "outer_z", -32768, 32767);
    p.outer_top = parse::integer_of(oct, "outer_top", -32768, 32767);
    p.ring_slot = parse::integer_of(oct, "ring_slot", 0, 4095);
    p.band_slot = parse::integer_of(oct, "band_slot", 0, 4095);
    const Value& colors = array_of(oct, "colors", 4);
    for (std::size_t i = 0; i < 4; i++) {
        const Value& prim = colors.array()[i];
        if (!prim.is_array() || prim.array().size() != 4) {
            parse::fail(prim, "a primitive's colours are four [r, g, b] corners");
        }
        for (std::size_t k = 0; k < 4; k++) {
            std::uint8_t c[3];
            parse::rgb(prim.array()[k], c);
            p.octagon_colors[i][k] = Rgb{c[0], c[1], c[2]};
        }
    }

    const Value& scroll = parse::member(root, "scroll");
    parse::reject_unknown(scroll, {"tick", "period"});
    p.scroll_tick = parse::integer_of(scroll, "tick", 1, 1 << 16);
    p.scroll_period = parse::integer_of(scroll, "period", 1, 1 << 16);

    const Value& surface = parse::member(root, "surface");
    parse::reject_unknown(surface, {"material", "base_color", "lit_color", "wave_length", "wave_contrast",
                                    "wave_distortion", "reflection", "glow", "quality"});
    const Value& material = parse::member(surface, "material");
    if (!material.is_string() || material.string().empty()) {
        parse::fail(material, "a material name is expected");
    }
    p.surface_material = material.string();
    parse::rgb(parse::member(surface, "base_color"), p.surface_base.data());
    parse::rgb(parse::member(surface, "lit_color"), p.surface_lit.data());
    p.wave_length = real_of(surface, "wave_length", 0.01, 100.0);
    p.wave_contrast = real_of(surface, "wave_contrast", 0.0, 1.0);
    p.wave_distortion = real_of(surface, "wave_distortion", 0.0, 1.0);
    const Value& reflection = parse::member(surface, "reflection");
    parse::reject_unknown(reflection, {"strength", "tint", "blur", "plane_offset"});
    p.reflection_strength = real_of(reflection, "strength", 0.0, 8.0);
    parse::rgb(parse::member(reflection, "tint"), p.reflection_tint.data());
    p.reflection_blur = real_of(reflection, "blur", 0.0, 16.0);
    p.reflection_plane_offset = real_of(reflection, "plane_offset", -100.0, 100.0);
    const Value& glow = parse::member(surface, "glow");
    parse::reject_unknown(glow, {"strength", "color", "size"});
    p.glow_strength = real_of(glow, "strength", 0.0, 8.0);
    parse::rgb(parse::member(glow, "color"), p.glow_color.data());
    p.glow_size = real_of(glow, "size", 0.01, 16.0);
    const Value& quality = parse::member(surface, "quality");
    parse::reject_unknown(quality, {"low", "medium", "high"});
    constexpr std::string_view kLevels[] = {"low", "medium", "high"};
    for (std::size_t i = 0; i < p.quality.size(); i++) {
        const Value& level = parse::member(quality, kLevels[i]);
        parse::reject_unknown(level, {"wave_layers", "wave_grid", "color_steps", "phase_steps", "distortion",
                                      "reflection_lines", "reflection_filter", "blur_taps"});
        WaterQuality& q = p.quality[i];
        q.wave_layers = parse::integer_of(level, "wave_layers", 1, 3);
        q.wave_grid = real_of(level, "wave_grid", 0.0, 1024.0);
        q.color_steps = parse::integer_of(level, "color_steps", 0, 256);
        q.phase_steps = parse::integer_of(level, "phase_steps", 0, 1 << 16);
        q.distortion = real_of(level, "distortion", 0.0, 1.0);
        q.reflection_lines = parse::integer_of(level, "reflection_lines", 0, 2160);
        if (q.reflection_lines != 0 && q.reflection_lines < 16) {
            parse::fail(parse::member(level, "reflection_lines"), "0 or 16 to 2160 is expected");
        }
        const Value& filter = parse::member(level, "reflection_filter");
        if (!filter.is_string() || (filter.string() != "nearest" && filter.string() != "linear")) {
            parse::fail(filter, "\"nearest\" or \"linear\" is expected");
        }
        q.reflection_smooth = filter.string() == "linear";
        q.blur_taps = parse::integer_of(level, "blur_taps", 1, 5);
        if (q.blur_taps % 2 == 0) {
            parse::fail(parse::member(level, "blur_taps"), "1, 3 or 5 is expected");
        }
    }
    return Result<WaterParams>::ok(std::move(p));
} catch (const std::exception& e) {
    return Result<WaterParams>::fail(e.what());
}

Result<WaterParams> load_water_params(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source) {
    const Result<std::string> text = read_effect_file(gamedata_dir, mods_dir, "effects/water.json", source);
    if (!text) {
        return Result<WaterParams>::fail(text.error());
    }
    Result<WaterParams> parsed = parse_water_params(text.value());
    if (!parsed) {
        return Result<WaterParams>::fail((source != nullptr ? *source + ": " : std::string()) + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
