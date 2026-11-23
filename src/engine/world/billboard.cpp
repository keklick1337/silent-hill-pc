// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/billboard.h"

#include <algorithm>

#include "engine/world/fixed_math.h"

namespace sh::world {

namespace {

// The four fixed normals the corners are lit with: (0, 1, 1), (-1, 0, 1), (1, 0, 1), (0, -1, 1) scaled by 255.65
// degrees' worth of Q12 angle (the original's constants).
constexpr std::int16_t angle_q12(float degrees) {
    return static_cast<std::int16_t>(static_cast<std::int32_t>(degrees / 360.0f * 4096.0f));
}

const std::array<math::Vec3s, 4>& corner_normals() {
    static const std::array<math::Vec3s, 4> normals = {{
        {angle_q12(0.0f), angle_q12(255.65f), angle_q12(255.65f)},
        {angle_q12(-255.65f), angle_q12(0.0f), angle_q12(255.65f)},
        {angle_q12(255.65f), angle_q12(0.0f), angle_q12(255.65f)},
        {angle_q12(0.0f), angle_q12(-255.65f), angle_q12(255.65f)},
    }};
    return normals;
}

}  // namespace

BillboardLightOut billboard_light(const BillboardLightIn& in) {
    BillboardLightOut out;
    out.light = in.light;
    out.background = in.background;
    if (in.mode == 0) {
        const auto channel = [&](std::uint8_t v) {
            return static_cast<std::uint8_t>((static_cast<std::int32_t>(v) * in.map_light) >> 12);
        };
        const math::Rgb flat{channel(in.tint.r), channel(in.tint.g), channel(in.tint.b)};
        out.corners = {flat, flat, flat, flat};
        return out;
    }

    // Length of the sun direction (first row of the light matrix); long directions are brought towards 1.
    std::int32_t x = in.light.m[0][0], y = in.light.m[0][1], z = in.light.m[0][2];
    const std::int32_t length = game_sqrt(wrap_add(wrap_add(wrap_mul(x, x), wrap_mul(y, y)), wrap_mul(z, z)));
    if (length > 0x400) {
        const std::int32_t eased = std::min<std::int32_t>(((length - 0x400) * 0x4cc >> 12) + 0x400, 0x1000);
        const std::int32_t ratio = eased * 0x1000 / length;
        x = (x * ratio) >> 12;
        y = (y * ratio) >> 12;
        z = (z * ratio) >> 12;
        if (in.mode == 1) {
            // The background colour is scaled by the same ratio.
            const auto ir = [](std::int32_t v) { return static_cast<std::int16_t>(v); };
            const math::ColorOut scaled = math::gpf({ir(in.background.x), ir(in.background.y), ir(in.background.z)},
                                                  static_cast<std::int16_t>(ratio));
            out.background = scaled.full;
        }
    }

    auto& m = out.light.m;
    m[0][0] = static_cast<std::int16_t>(x);
    m[0][1] = static_cast<std::int16_t>(y);
    m[0][2] = static_cast<std::int16_t>(z);
    m[1][0] = static_cast<std::int16_t>(-z >> 4);
    m[1][1] = static_cast<std::int16_t>(y >> 4);
    m[1][2] = static_cast<std::int16_t>(x >> 4);
    m[2][0] = static_cast<std::int16_t>(z >> 4);
    m[2][1] = static_cast<std::int16_t>(y >> 4);
    m[2][2] = static_cast<std::int16_t>(-x >> 4);

    const auto& normals = corner_normals();
    for (std::size_t i = 0; i < 4; ++i) {
        out.corners[i] = math::normal_color(out.light, in.color_matrix, out.background, normals[i], true).rgb;
    }
    return out;
}

BillboardQuad billboard_quad(BillboardView& view, const BillboardPoint& point) {
    BillboardQuad q;
    const math::Vec3s v{point.x, point.y, point.z};
    math::Projected p;
    if (view.whole_map_far) {
        const math::ProjectedFar far = math::project_vertex_far(view.projection, v);
        p = far.v;
        view.last_depth = far.unclamped_depth;
    } else {
        p = math::project_vertex(view.projection, v);
    }
    q.depth = p.sz >> 2;
    std::int32_t full = q.depth << 2;
    // Beyond about 256 units the depth register is pegged: a far sprite keeps its true depth so that it shrinks.
    if (full >= 0xfffc && view.whole_map_draw && view.last_depth > full) {
        full = view.last_depth;
    }
    q.full_depth = full;
    if (!(q.depth > 32 && full < view.depth_limit)) {
        return q;
    }
    q.visible = true;
    // Q12(h) in single precision, as the original computed it.
    const std::int32_t h_q12 = static_cast<std::int32_t>(static_cast<float>(view.projection.h) * 4096.0f);
    const std::int32_t scale = h_q12 / full;
    const std::int32_t up = (point.height * scale) >> 12;
    const std::int32_t side = (point.half_width * scale) >> 12;
    const std::int32_t cx = p.sx, cy = p.sy;
    const auto s16 = [](std::int32_t v) { return static_cast<std::int16_t>(v); };
    q.x = {s16(cx), s16(cx + side), s16(cx - side), s16(cx)};
    q.y = {s16(cy - up * 2), s16(cy - up), s16(cy - up), s16(cy)};
    return q;
}

std::int32_t billboard_depth_limit(std::int32_t scaled_base, bool fog_enabled, std::int32_t fog_far, bool whole_map_draw) {
    if (whole_map_draw) {
        return 0x7fffffff;
    }
    return fog_enabled ? std::min(scaled_base, fog_far) : scaled_base;
}

std::uint8_t billboard_fog_byte(std::int32_t ramp_u8, std::int32_t intensity) {
    std::int32_t amount = ramp_u8 * 16 + intensity;
    if (amount > 0x1000) {
        amount = 0x1000;
    }
    return static_cast<std::uint8_t>((amount * 127 + 2048) >> 12);
}

}  // namespace sh::world
