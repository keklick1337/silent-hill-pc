// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/world_env.h"

#include <algorithm>
#include <bit>

#include "formats/common/json.h"

namespace sh::world {

namespace {

constexpr std::int32_t kOne = 4096;

// 32-bit wrapping arithmetic: the game stores these values in 32 bits.
std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
std::int32_t shl(std::int32_t v, int n) {
    return n >= 32 ? 0 : static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << n);
}
std::int32_t q12_mul(std::int32_t a, std::int32_t b) { return wrap_mul(a, b) >> 12; }

int leading_sign_bits(std::int32_t v) {
    const std::uint32_t u = static_cast<std::uint32_t>(v < 0 ? ~v : v);
    return u == 0 ? 32 : std::countl_zero(u);
}

}  // namespace

FogRamp fog_ramp_build(const WorldEnvParams& params, std::int32_t near_q8, std::int32_t far_q8) {
    FogRamp fog;
    if (near_q8 < 1) {
        near_q8 = 1;
    }
    fog.near_distance = near_q8;
    fog.far_distance = far_q8;
    fog.depth_shift = 32 - leading_sign_bits(near_q8 - 1);

    const std::int32_t step = shl(0x10, fog.depth_shift + 1) / near_q8;
    int bin = 0;
    // `step` is at least 32 for any distance the game uses; guard the loop anyway.
    for (std::int32_t t = 0; t < kOne && bin < kFogRampSize && step > 0; t += step, ++bin) {
        const std::int32_t slot = t >> 8;
        std::int32_t span, frac, lo, hi;
        if (slot == 0) {
            span = 0x80;
            frac = t & 0x7F;
            if (t & 0x80) {
                lo = 0;
                hi = params.fog_curve[0];
            } else {
                lo = params.fog_curve[0];
                hi = params.fog_curve[1];
            }
        } else {
            span = 0x100;
            frac = t & 0xFF;
            lo = params.fog_curve[slot];
            hi = params.fog_curve[slot + 1];
        }
        const std::int32_t v = (lo * (span - frac) + hi * frac) / span;
        fog.ramp[bin] = static_cast<std::uint8_t>(std::clamp(v, 0, 0xFF));
    }
    for (; bin < kFogRampSize; ++bin) {
        fog.ramp[bin] = 0xFF;
    }
    return fog;
}

std::int32_t fog_ramp_q12(const FogRamp& fog, std::int32_t depth) {
    std::int32_t bin_size = shl(1, fog.depth_shift) >> 7;
    if (shl(1, fog.depth_shift) < 0) {
        bin_size = (shl(1, fog.depth_shift) + 0x7F) >> 7;
    }
    if (bin_size == 0) {
        return kOne;
    }
    const std::int32_t d = depth >> 4;
    const std::int32_t index = d / bin_size;
    if (index < 0) {
        return 0;
    }
    if (index >= kFogRampSize) {
        return kOne;
    }
    const std::int32_t lo = fog.ramp[index] * 16;
    const std::int32_t rem = d % bin_size;
    const std::int32_t hi = index == kFogRampSize - 1 ? kOne : fog.ramp[index + 1] * 16;
    return lo + ((hi - lo) * rem) / bin_size;
}

std::uint8_t fog_ramp_u8(const FogRamp& fog, std::int32_t depth) {
    const std::int32_t d = depth >> 4;
    if (d >= shl(1, fog.depth_shift)) {
        return 255;
    }
    return fog.ramp[(shl(d, 7) >> fog.depth_shift) & (kFogRampSize - 1)];
}

LightFalloffTable light_falloff_compute(const WorldEnvParams& params, std::int32_t intensity,
                                        const std::array<std::int32_t, 3>& dir) {
    LightFalloffTable table{};
    for (int axis = 0; axis < 3; ++axis) {
        const std::int32_t a = dir[axis];
        const std::int32_t w_pos = wrap_mul(a + params.light_axis_bias, kOne) / params.light_axis_span;
        const std::int32_t w_neg = wrap_mul(params.light_axis_bias - a, kOne) / params.light_axis_span;
        const std::int32_t inv_pos = w_pos == 0 ? 0 : (kOne * 1024) / w_pos;
        const std::int32_t inv_neg = w_neg == 0 ? 0 : (kOne * 1024) / w_neg;
        const std::int32_t sx = params.light_sample[0];
        const std::int32_t sy = params.light_sample[1];
        const std::int32_t sz = params.light_sample[2];
        for (int side = 0; side < 2; ++side) {
            LightFalloff& f = table[axis][side];
            const std::int32_t w = side == 0 ? w_pos : w_neg;
            const std::int32_t inv = side == 0 ? inv_pos : inv_neg;
            f.base = q12_mul(sy, intensity);
            f.x = q12_mul(sx, w);
            f.slope = q12_mul(wrap_mul(sz, inv) >> 10, intensity);
        }
    }
    return table;
}

std::uint8_t darkness_at(const LightFalloffTable& table, bool lit_mode, std::int32_t ambient, std::int32_t cap,
                         const std::array<std::int32_t, 3>& offset) {
    std::int32_t result = 0;
    if (lit_mode) {
        result = 0xFF;
        for (int axis = 0; axis < 3; ++axis) {
            std::int32_t d = offset[axis];
            int side = 0;
            if (d < 0) {
                d = -d;
                side = 1;
            }
            const LightFalloff& f = table[axis][side];
            const std::int32_t v = f.base + q12_mul(f.slope, d - f.x);
            result = std::min(result, v);
        }
    }
    result += ambient >> 5;
    result = std::clamp(result, 0, cap);
    return static_cast<std::uint8_t>(result);
}

namespace {

std::int16_t scale16(std::int16_t v, std::uint8_t c) { return static_cast<std::int16_t>(static_cast<std::int32_t>(v) * c / 255); }

}  // namespace

LightSetup light_setup(std::uint8_t mode, std::int32_t ambient, std::uint8_t cap, const std::array<std::int32_t, 3>& tint,
                       std::int32_t brightness, const ColorScale& light_override, const ColorScale& world_override) {
    LightSetup s;
    s.mode = mode;
    s.ambient = ambient;
    s.cap = cap;
    s.brightness = brightness;
    for (int c = 0; c < 3; ++c) {
        s.world_tint[c] = static_cast<std::uint8_t>(tint[c] >> 5);
        s.light_color[c] = static_cast<std::int16_t>(tint[c]);
        s.char_light[c] = static_cast<std::uint8_t>((tint[c] * ambient) >> 17);
    }
    if (light_override.active) {
        s.light_color[0] = scale16(s.light_color[0], light_override.r);
        s.light_color[1] = scale16(s.light_color[1], light_override.g);
        s.light_color[2] = scale16(s.light_color[2], light_override.b);
    }
    if (world_override.active) {
        const std::uint8_t k[3] = {world_override.r, world_override.g, world_override.b};
        for (int c = 0; c < 3; ++c) {
            s.world_tint[c] = static_cast<std::uint8_t>(static_cast<std::int32_t>(s.world_tint[c]) * k[c] / 255);
            s.char_light[c] = static_cast<std::uint8_t>(scale16(s.char_light[c], k[c]));
        }
    }
    return s;
}

Result<WorldEnvParams> WorldEnvParams::parse(std::string_view text) {
    using json::Value;
    Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<WorldEnvParams>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* fmt = root.find("format");
    if (!root.is_object() || !fmt || !fmt->is_string() || fmt->string() != "sh-world-env") {
        return Result<WorldEnvParams>::fail("not a sh-world-env file");
    }
    WorldEnvParams p;
    const Value* fog = root.find("fog_curve");
    if (!fog || !fog->is_array() || fog->array().size() != p.fog_curve.size()) {
        return Result<WorldEnvParams>::fail("\"fog_curve\" is an array of 17 integers");
    }
    for (std::size_t i = 0; i < p.fog_curve.size(); ++i) {
        const Value& v = fog->array()[i];
        if (!v.is_integer() || v.integer() < 0 || v.integer() > 255) {
            return Result<WorldEnvParams>::fail("\"fog_curve\" values are integers 0..255");
        }
        p.fog_curve[i] = static_cast<std::int32_t>(v.integer());
    }
    const Value* lt = root.find("light");
    if (!lt || !lt->is_object()) {
        return Result<WorldEnvParams>::fail("\"light\" is required");
    }
    auto number = [&](const char* key, std::int32_t& out, std::int64_t lo) -> std::string {
        const Value* v = lt->find(key);
        if (!v || !v->is_integer() || v->integer() < lo || v->integer() > 0x7FFFFFF) {
            return std::string("light: \"") + key + "\" is an integer in range";
        }
        out = static_cast<std::int32_t>(v->integer());
        return {};
    };
    if (std::string e = number("axis_bias", p.light_axis_bias, 0); !e.empty()) {
        return Result<WorldEnvParams>::fail(e);
    }
    if (std::string e = number("axis_span", p.light_axis_span, 1); !e.empty()) {
        return Result<WorldEnvParams>::fail(e);
    }
    const Value* smp = lt->find("sample");
    if (!smp || !smp->is_array() || smp->array().size() != 3) {
        return Result<WorldEnvParams>::fail("light: \"sample\" is an array of 3 integers");
    }
    for (int i = 0; i < 3; ++i) {
        const Value& v = smp->array()[i];
        if (!v.is_integer() || v.integer() < -0x7FFFFFF || v.integer() > 0x7FFFFFF) {
            return Result<WorldEnvParams>::fail("light: \"sample\" values are integers");
        }
        p.light_sample[i] = static_cast<std::int32_t>(v.integer());
    }
    return Result<WorldEnvParams>::ok(p);
}

}  // namespace sh::world
