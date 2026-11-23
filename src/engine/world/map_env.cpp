// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/map_env.h"

#include <algorithm>
#include <bit>

#include "formats/common/json.h"

namespace sh::world {

namespace {

constexpr std::int32_t kOne = 4096;

std::int32_t clamp_q12(std::int32_t v) { return std::clamp(v, 0, kOne); }

// 32-bit wrapping products and shifts, as the values are stored in 32 bits.
std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
std::int32_t shl(std::int32_t v, int n) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << n); }

// Leading sign bits of a 32-bit value (32 for 0 and -1).
int leading_sign_bits(std::int32_t v) {
    const std::uint32_t u = static_cast<std::uint32_t>(v < 0 ? ~v : v);
    return u == 0 ? 32 : std::countl_zero(u);
}

std::uint8_t mix_channel(std::uint8_t a, std::uint8_t b, std::int32_t wa, std::int32_t wb) {
    const std::int32_t v = (a * wa + b * wb) >> 12;
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

EnvColor mix(const EnvColor& a, const EnvColor& b, std::int32_t to) {
    const std::int32_t from = kOne - to;
    return {mix_channel(a.r, b.r, from, to), mix_channel(a.g, b.g, from, to), mix_channel(a.b, b.b, from, to)};
}

std::int16_t avg16(std::int16_t a, std::int16_t b, std::int32_t w) {
    return static_cast<std::int16_t>(weighted_average(a, b, w));
}

// Shares of a blend: `all` is the plain weight, the others are the weights of its two halves.
void blend_levels(EnvZone& out, const EnvZone& a, const EnvZone& b, std::int32_t w_zone, std::int32_t w_fog,
                  std::int32_t w_color) {
    if (a.brightness != b.brightness) {
        out.brightness = avg16(a.brightness, b.brightness, w_zone);
    } else {
        out.brightness = b.brightness;
    }
    out.fog_distance = weighted_average(a.fog_distance, b.fog_distance, w_zone);
    out.info.fog_distance = weighted_average(a.info.fog_distance, b.info.fog_distance, w_fog);
    out.info.tint_intensity = avg16(a.info.tint_intensity, b.info.tint_intensity, w_zone);
    out.info.fog_color = mix(a.info.fog_color, b.info.fog_color, w_color);
}

void blend_screen_tint(EnvInfo& out, const EnvInfo& a, const EnvInfo& b, std::int32_t to) {
    out.screen_tint_add = mix(a.screen_tint_add, b.screen_tint_add, to);
    out.screen_tint_sub = mix(a.screen_tint_sub, b.screen_tint_sub, to);
    const bool any = out.screen_tint_add.r || out.screen_tint_add.g || out.screen_tint_add.b ||
                     out.screen_tint_sub.r || out.screen_tint_sub.g || out.screen_tint_sub.b;
    out.screen_tint_on = any ? 1 : 0;
}

}  // namespace

std::int32_t progress_alpha(std::int32_t value, std::int32_t from, std::int32_t to) {
    if (from < to) {
        value = std::clamp(value, from, to);
    } else if (to < from) {
        value = std::clamp(value, to, from);
    } else {
        return kOne;
    }
    // Keep the numerator in 32 bits: spans wider than 19 integer bits lose their low bits first.
    const int bits = 32 - leading_sign_bits(to - from);
    int shift = bits + 12 >= 31 ? bits - 19 : 0;
    shift = std::clamp(shift, 0, 12);
    const std::int32_t divisor = (to - from) >> shift;
    if (divisor == 0) {
        return kOne;
    }
    return shl(value - from, 12 - shift) / divisor;
}

std::int32_t weighted_average(std::int32_t a, std::int32_t b, std::int32_t weight) {
    const auto mul = [](std::int32_t x, std::int32_t y) {
        return static_cast<std::int32_t>((static_cast<std::int64_t>(x) * y) >> 12);
    };
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(mul(a, kOne - weight)) +
                                     static_cast<std::uint32_t>(mul(b, weight)));
}

void fog_params(EnvZone& zone, const EnvInfo& info) {
    zone.info = info;
    zone.brightness = (info.flags & kEnvFlagBrightness) ? kOne : 0;
    zone.lens_flare = (info.flags & kEnvFlagLensFlare) ? kOne : 0;
    switch (info.fog_mode) {
        case 0:
        case 1:
        case 3:
            zone.fog_distance = info.fog_distance;
            break;
        case 2:
            zone.fog_distance = 0;
            break;
        default:
            break;  // another mode leaves the distance as it was
    }
}

void blend_zone(const MapEnvParams& params, EnvZone& out, const EnvZone& a, const EnvZone& b, std::int32_t weight) {
    const std::int32_t w_first = clamp_q12(weight * 2);           // first half of the weight
    const std::int32_t w_second = clamp_q12((weight - kOne / 2) * 2);  // second half
    EnvInfo& o = out.info;
    const EnvInfo& ia = a.info;
    const EnvInfo& ib = b.info;

    o.flags = weight < kOne / 2 ? ia.flags : ib.flags;
    o.particle_color_a = mix(ia.particle_color_a, ib.particle_color_a, weight);
    o.particle_color_b = mix(ia.particle_color_b, ib.particle_color_b, weight);

    if (a.lens_flare == 0) {
        out.lens_flare = avg16(0, b.lens_flare, w_second);
    } else {
        out.lens_flare = avg16(a.lens_flare, b.lens_flare, w_first);
    }

    // The light level is blended from the half the two flag bits point to.
    const bool a_late = (ia.flags & kEnvFlagBit0) != 0;
    const bool b_late = (ib.flags & kEnvFlagBit0) != 0;
    std::int32_t level_w = weight;
    if (a_late && !b_late) {
        level_w = w_second;
    } else if (!a_late && b_late) {
        level_w = w_first;
    }
    o.light_level = static_cast<std::uint8_t>(weighted_average(ia.light_level, ib.light_level, level_w));

    if (ia.fog_mode == 0) {
        if (ib.fog_mode != 0) {
            o.fog_mode = ib.fog_mode;
            blend_levels(out, a, b, weight, w_first, w_second);
        } else {
            o.fog_mode = ib.fog_mode;
            blend_levels(out, a, b, weight, weight, weight);
        }
    } else if (ib.fog_mode == 0) {
        o.fog_mode = w_second >= kOne ? ib.fog_mode : ia.fog_mode;
        blend_levels(out, a, b, weight, w_second, w_first);
    } else {
        o.fog_mode = ib.fog_mode;
        blend_levels(out, a, b, weight, weight, weight);
    }

    o.tint_r = avg16(ia.tint_r, ib.tint_r, weight);
    o.tint_g = avg16(ia.tint_g, ib.tint_g, weight);
    o.tint_b = avg16(ia.tint_b, ib.tint_b, weight);

    if (ia.light_mode == 1 && ib.light_mode == 2) {
        if (weight < params.to_dense_pivot) {
            const std::int32_t w = clamp_q12(wrap_mul(weight, params.long_rate) >> 12);
            o.light_mode = ia.light_mode;
            o.alpha = avg16(ia.alpha, 0, w);
        } else {
            const std::int32_t w = clamp_q12(wrap_mul(weight - params.to_dense_pivot, 6));
            o.light_mode = ib.light_mode;
            o.alpha = avg16(0, ib.alpha, w);
        }
    } else if (ia.light_mode == 2 && ib.light_mode == 1) {
        if (weight < params.from_dense_pivot) {
            const std::int32_t w = clamp_q12(wrap_mul(weight, 6));
            o.light_mode = ia.light_mode;
            o.alpha = avg16(ia.alpha, 0, w);
        } else {
            const std::int32_t w = clamp_q12(wrap_mul(weight - params.from_dense_pivot, params.long_rate) >> 12);
            o.light_mode = ib.light_mode;
            o.alpha = avg16(0, ib.alpha, w);
        }
    } else {
        if (ia.light_mode != 0 && ib.light_mode == 0) {
            o.light_mode = weight >= kOne ? ib.light_mode : ia.light_mode;
        } else {
            o.light_mode = ib.light_mode;
        }
        o.alpha = avg16(ia.alpha, ib.alpha, weight);
    }

    blend_screen_tint(o, ia, ib, (ia.screen_tint_on == 0 && ib.screen_tint_on != 0) ? w_second : weight);
}

std::int32_t fog_distance(const MapEnvParams& params, const EnvInfo& info, bool fog_enabled, bool dense_light) {
    if (fog_enabled) {
        return info.fog_distance;
    }
    if (!dense_light) {
        return params.open_fog_distance;
    }
    // Polyline through the table over 0..alpha_span, integer steps.
    const auto& y = params.fog_by_alpha;
    const std::int32_t n = static_cast<std::int32_t>(y.size());
    const std::int32_t x = info.alpha;
    if (x >= params.alpha_span) {
        return y[n - 1];
    }
    if (x < 0) {
        return y[0];
    }
    const std::int32_t width = params.alpha_span / (n - 1);
    const std::int32_t rest = x % width;
    const std::int32_t step = x / width;
    if (step >= n - 1) {
        return y[n - 1];
    }
    return (y[step] * (width - rest) + y[step + 1] * rest) / width;
}

Result<MapEnvParams> MapEnvParams::parse(std::string_view text) {
    using json::Value;
    Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<MapEnvParams>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* fmt = root.find("format");
    if (!root.is_object() || !fmt || !fmt->is_string() || fmt->string() != "sh-map-effects") {
        return Result<MapEnvParams>::fail("not a sh-map-effects file");
    }
    MapEnvParams p;
    auto number = [](const Value& parent, const char* key, std::int32_t& out) -> std::string {
        const Value* v = parent.find(key);
        if (!parent.is_object() || !v || !v->is_integer() || v->integer() < 0 || v->integer() > 0x7FFFFFF) {
            return std::string("\"") + key + "\" is a non-negative integer";
        }
        out = static_cast<std::int32_t>(v->integer());
        return {};
    };
    const Value* cf = root.find("light_mode_cross_fade");
    const Value* fa = root.find("fog_by_alpha");
    if (!cf || !fa) {
        return Result<MapEnvParams>::fail("\"light_mode_cross_fade\" and \"fog_by_alpha\" are required");
    }
    for (const auto& [key, dst] : {std::pair<const char*, std::int32_t*>{"to_dense_pivot", &p.to_dense_pivot},
                                   {"from_dense_pivot", &p.from_dense_pivot},
                                   {"long_rate", &p.long_rate}}) {
        if (std::string e = number(*cf, key, *dst); !e.empty()) {
            return Result<MapEnvParams>::fail("light_mode_cross_fade: " + e);
        }
    }
    if (std::string e = number(*fa, "alpha_span", p.alpha_span); !e.empty()) {
        return Result<MapEnvParams>::fail("fog_by_alpha: " + e);
    }
    if (std::string e = number(*fa, "open_fog_distance", p.open_fog_distance); !e.empty()) {
        return Result<MapEnvParams>::fail("fog_by_alpha: " + e);
    }
    const Value* d = fa->find("distances");
    if (!d || !d->is_array() || d->array().size() != p.fog_by_alpha.size()) {
        return Result<MapEnvParams>::fail("fog_by_alpha: \"distances\" has exactly 5 numbers");
    }
    for (std::size_t i = 0; i < p.fog_by_alpha.size(); ++i) {
        const Value& v = d->array()[i];
        if (!v.is_integer() || v.integer() < 0 || v.integer() > 0x7FFFFFF) {
            return Result<MapEnvParams>::fail("fog_by_alpha: \"distances\" holds non-negative integers");
        }
        p.fog_by_alpha[i] = static_cast<std::int32_t>(v.integer());
    }
    if (p.alpha_span < 4) {
        return Result<MapEnvParams>::fail("fog_by_alpha: \"alpha_span\" is at least 4");
    }
    return Result<MapEnvParams>::ok(p);
}

namespace {

EnvColor color_of(const game::Rgbx& c) { return {c.r, c.g, c.b}; }

void store_color(game::Rgbx& c, const EnvColor& v) {
    c.r = v.r;
    c.g = v.g;
    c.b = v.b;
}

}  // namespace

EnvInfo env_info_of(const game::EnvironmentPreset& p) {
    EnvInfo e;
    e.flags = p.flags;
    e.light_level = p.light_level_max;
    e.light_mode = p.light_mode;
    e.alpha = p.spot_light_intensity;
    e.tint_intensity = p.world_light_intensity;
    e.tint_r = p.world_tint[0];
    e.tint_g = p.world_tint[1];
    e.tint_b = p.world_tint[2];
    e.fog_mode = p.fog_mode;
    e.fog_distance = p.fog_distance;
    e.fog_color = color_of(p.fog_color);
    e.screen_tint_on = p.tint_light_overlap;
    e.screen_tint_add = color_of(p.point_light_tint);
    e.screen_tint_sub = color_of(p.world_tint_color);
    e.particle_color_a = color_of(p.snow_color);
    e.particle_color_b = color_of(p.rain_color);
    return e;
}

EnvZone env_zone_of(const game::EnvironmentState& s) {
    EnvZone z;
    z.info = env_info_of(s.preset);
    z.lens_flare = s.lens_flare_intensity;
    z.brightness = s.brightness_intensity;
    z.fog_distance = s.fog_distance;
    return z;
}

void store_env_zone(game::EnvironmentState& s, const EnvZone& z) {
    game::EnvironmentPreset& p = s.preset;
    const EnvInfo& e = z.info;
    p.flags = e.flags;
    p.light_level_max = e.light_level;
    p.light_mode = e.light_mode;
    p.spot_light_intensity = e.alpha;
    p.world_light_intensity = e.tint_intensity;
    p.world_tint[0] = e.tint_r;
    p.world_tint[1] = e.tint_g;
    p.world_tint[2] = e.tint_b;
    p.fog_mode = e.fog_mode;
    p.fog_distance = e.fog_distance;
    store_color(p.fog_color, e.fog_color);
    p.tint_light_overlap = e.screen_tint_on;
    store_color(p.point_light_tint, e.screen_tint_add);
    store_color(p.world_tint_color, e.screen_tint_sub);
    store_color(p.snow_color, e.particle_color_a);
    store_color(p.rain_color, e.particle_color_b);
    s.lens_flare_intensity = z.lens_flare;
    s.brightness_intensity = z.brightness;
    s.fog_distance = z.fog_distance;
}

void set_preset_state(game::EnvironmentState& state, const game::EnvironmentPreset& preset) {
    state.preset = preset;
    EnvZone z = env_zone_of(state);
    fog_params(z, env_info_of(preset));
    store_env_zone(state, z);
}

void blend_state(const MapEnvParams& params, game::EnvironmentState& out, const game::EnvironmentState& a,
                 const game::EnvironmentState& b, std::int32_t weight) {
    EnvZone z = env_zone_of(out);
    blend_zone(params, z, env_zone_of(a), env_zone_of(b), weight);
    store_env_zone(out, z);
}

}  // namespace sh::world
