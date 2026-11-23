// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/world_atmosphere.h"

#include "engine/math/fixed_geometry.h"

namespace sh::world {

namespace detail {
AtmosphereState g_atmosphere_state;
}  // namespace detail

namespace {

constexpr std::int32_t kOne = 4096;

math::Rgb rgb_of(const Color8& c) { return {c.r, c.g, c.b}; }

Color8 color_of(const math::ColorOut& out, std::uint8_t code) { return {out.rgb.r, out.rgb.g, out.rgb.b, code}; }

// One value in all three lanes of the intensity vector; the lanes are 16 bits.
math::Vec3s lanes(std::int32_t value) {
    const auto v = static_cast<std::int16_t>(value);
    return {v, v, v};
}

math::Matrix3 transposed(const ModelFrame& model) {
    math::Matrix3 t;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            t.m[row][col] = model.m[col][row];
        }
    }
    return t;
}

}  // namespace

void WorldAtmosphere::reset() {
    AtmosphereState& s = *state_;
    s.light_mode = 0;
    s.ambient = kOne;
    s.world_tint.r = s.world_tint.g = s.world_tint.b = 128;
    s.fog_enabled = 0;
    s.tint_light_overlap = 0;
    s.fog.color.r = s.fog.color.g = s.fog.color.b = 255;
    s.light_depth_cue = 0;
    s.lens_flare_intensity = 0;
    s.water_zones = nullptr;
    s.fog.intensity = 0;
    set_fog_distance(32 * kOne, 34 * kOne);
}

void WorldAtmosphere::set_fog(bool enabled, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    state_->fog_enabled = enabled ? 1 : 0;
    state_->fog.color.r = r;
    state_->fog.color.g = g;
    state_->fog.color.b = b;
}

void WorldAtmosphere::set_fog_distance(std::int32_t near_q12, std::int32_t far_q12) {
    last_fog_near_ = near_q12;
    last_fog_far_ = far_q12;
    if (fog_scale_pct_ != 100 && fog_scale_pct_ > 0) {
        near_q12 = static_cast<std::int32_t>(static_cast<std::int64_t>(near_q12) * fog_scale_pct_ / 100);
        far_q12 = static_cast<std::int32_t>(static_cast<std::int64_t>(far_q12) * fog_scale_pct_ / 100);
    }
    const FogRamp fog = fog_ramp_build(*params_, near_q12 >> 4, far_q12 >> 4);
    state_->fog.near_distance = fog.near_distance;
    state_->fog.far_distance = fog.far_distance;
    state_->fog.depth_shift = fog.depth_shift;
    state_->fog_ramp = fog.ramp;
}

void WorldAtmosphere::reapply_fog_distance() {
    if (last_fog_far_ > 0) {
        set_fog_distance(last_fog_near_, last_fog_far_);
    }
}

FogRamp WorldAtmosphere::ramp() const {
    FogRamp f;
    f.near_distance = state_->fog.near_distance;
    f.far_distance = state_->fog.far_distance;
    f.depth_shift = state_->fog.depth_shift;
    f.ramp = state_->fog_ramp;
    return f;
}

void WorldAtmosphere::set_fog_intensity(std::int32_t depth) { state_->fog.intensity = kOne - fog_ramp_q12(depth); }

std::int32_t WorldAtmosphere::fog_ramp_q12(std::int32_t depth) const { return world::fog_ramp_q12(ramp(), depth); }

std::uint8_t WorldAtmosphere::fog_ramp_u8(std::int32_t depth) const { return world::fog_ramp_u8(ramp(), depth); }

std::int32_t WorldAtmosphere::fog_ramp_at(std::int32_t depth) const {
    const std::int32_t shift = state_->fog.depth_shift;
    if (depth < 0) {
        depth = 0;
    }
    if (depth < (1 << shift)) {
        return state_->fog_ramp[static_cast<std::size_t>((depth << 7) >> shift)];
    }
    return 255;
}

std::int32_t WorldAtmosphere::fog_cull_distance(bool modern_culling, float fog_strength) const {
    std::int32_t far = state_->fog.far_distance;
    if (modern_culling && fog_strength < 1.0f) {
        const float strength = fog_strength < 0.1f ? 0.1f : fog_strength;
        const float wide = static_cast<float>(far) / strength;
        far = wide > 2.0e9f ? 0x7FFFFFFF : static_cast<std::int32_t>(wide);
    }
    return far;
}

int WorldAtmosphere::fog_keep(std::int32_t z) const {
    if (!state_->fog_enabled) {
        return 256;
    }
    const std::int32_t shift = state_->fog.depth_shift;
    if (z < 0) {
        z = 0;
    }
    if (z >= (1 << shift)) {
        return 0;
    }
    std::int32_t index = (z << 7) >> shift;
    if (index > kFogRampSize - 1) {
        index = kFogRampSize - 1;
    }
    return 256 - state_->fog_ramp[static_cast<std::size_t>(index)];
}

void WorldAtmosphere::set_lighting(std::uint8_t mode, std::int32_t ambient, std::uint8_t cap,
                                   const std::array<std::int32_t, 3>& tint, std::int32_t brightness,
                                   const ColorScale& light_override, const ColorScale& world_override) {
    const LightSetup s = light_setup(mode, ambient, cap, tint, brightness, light_override, world_override);
    AtmosphereState& a = *state_;
    a.light_mode = s.mode;
    a.ambient = s.ambient;
    a.light_level_max = s.cap;
    a.screen_brightness = s.brightness;
    a.world_tint.r = s.world_tint[0];
    a.world_tint.g = s.world_tint[1];
    a.world_tint.b = s.world_tint[2];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            a.light_color.m[row][col] = s.light_color[row];
        }
    }
    a.char_light.r = s.char_light[0];
    a.char_light.g = s.char_light[1];
    a.char_light.b = s.char_light[2];
}

void WorldAtmosphere::set_light_falloff(std::int32_t intensity, const Vec3s16& direction) {
    const LightFalloffTable t = light_falloff_compute(*params_, intensity, {direction.x, direction.y, direction.z});
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            state_->falloff[axis][side] = {t[axis][side].x, t[axis][side].base, t[axis][side].slope};
        }
    }
}

void WorldAtmosphere::set_point_light(std::int32_t intensity, std::int16_t lens_flare, void* water_zones,
                                      const Vec3s16& direction, const Vec3i32& position, const Vec3s16& angles) {
    AtmosphereState& a = *state_;
    a.light_intensity = intensity;
    a.lens_flare_intensity = lens_flare;
    a.water_zones = water_zones;
    a.light_direction = direction;
    a.light_position = position;
    a.light_angles = angles;
    a.light_depth_cue = intensity >> 8;
    set_light_falloff(intensity, direction);
}

std::uint8_t WorldAtmosphere::darkness_at(std::int32_t x, std::int32_t y, std::int32_t z) const {
    const AtmosphereState& a = *state_;
    LightFalloffTable t{};
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            const FalloffEntry& e = a.falloff[axis][side];
            t[axis][side] = {e.x, e.y, e.z};
        }
    }
    const std::array<std::int32_t, 3> offset{(x >> 4) - (a.light_position.x >> 4), (y >> 4) - (a.light_position.y >> 4),
                                             (z >> 4) - (a.light_position.z >> 4)};
    return world::darkness_at(t, a.light_mode != 0, a.ambient, a.light_level_max, offset);
}

FogTint WorldAtmosphere::fog_tint(std::uint8_t level, std::int32_t depth_q12) const {
    const AtmosphereState& a = *state_;
    const std::int32_t fog = fog_ramp_at(depth_q12 >> 4) << 4;
    std::int32_t through = kOne - (a.fog.intensity + fog);
    if (through < 0) {
        through = 0;
    }
    const math::Vec3i no_far{};
    FogTint out;
    out.fog = color_of(math::dpcs(rgb_of(a.fog.color), static_cast<std::int16_t>(through), no_far),
                       a.fog.color.code);
    out.tint = color_of(math::dpcl(rgb_of(a.world_tint), lanes(level << 5), static_cast<std::int16_t>(fog), no_far),
                        a.world_tint.code);
    return out;
}

Color8 WorldAtmosphere::faded_tint(std::uint8_t code, std::uint8_t amount) const {
    return color_of(math::dpcs(rgb_of(state_->world_tint), static_cast<std::int16_t>(kOne - amount * 32), math::Vec3i{}),
                    code);
}

void WorldAtmosphere::set_model_light(const ModelFrame& model) {
    AtmosphereState& a = *state_;
    const math::Matrix3 to_model = transposed(model);

    const math::MatVec turned = math::matrix_vector(to_model, {a.light_direction.x, a.light_direction.y,
                                                             a.light_direction.z});
    const math::ColorOut scaled = math::gpf(turned.saturated, static_cast<std::int16_t>(a.light_intensity));
    a.local_light_direction.x = scaled.saturated.x;
    a.local_light_direction.y = scaled.saturated.y;
    a.local_light_direction.z = scaled.saturated.z;

    const math::Vec3s offset{static_cast<std::int16_t>((a.light_position.x >> 4) - model.t[0]),
                            static_cast<std::int16_t>((a.light_position.y >> 4) - model.t[1]),
                            static_cast<std::int16_t>((a.light_position.z >> 4) - model.t[2])};
    const math::MatVec rel = math::matrix_vector(to_model, offset);
    a.local_light_position.x = rel.saturated.x;
    a.local_light_position.y = rel.saturated.y;
    a.local_light_position.z = rel.saturated.z;
}

WorldAtmosphere::Fade WorldAtmosphere::fade_amount(const Vec3s16& point, const Vec3s16& normal,
                                                   const ModelFrame& model) {
    set_model_light(model);
    const AtmosphereState& a = *state_;

    Fade out;
    std::int16_t(&r)[3][3] = out.rotation;
    r[0][0] = a.local_light_direction.x;
    r[0][1] = a.local_light_direction.y;
    r[0][2] = a.local_light_direction.z;
    r[1][0] = static_cast<std::int16_t>(-normal.x);
    r[1][1] = static_cast<std::int16_t>(-normal.y);
    r[1][2] = static_cast<std::int16_t>(-normal.z);
    r[2][0] = static_cast<std::int16_t>(point.x - a.local_light_position.x);
    r[2][1] = static_cast<std::int16_t>(point.y - a.local_light_position.y);
    r[2][2] = static_cast<std::int16_t>(point.z - a.local_light_position.z);

    // Projecting the third row with a screen distance of 16 and a (-1024, -1024) offset gives, in the screen
    // coordinates and the depth cue, the point's alignment with the light and the facing of its normal.
    math::Projection proj;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            proj.rot.m[row][col] = r[row][col];
        }
    }
    proj.h = 16;
    proj.ofx = -1024 * 65536;
    proj.ofy = -1024 * 65536;
    proj.dqa = static_cast<std::int16_t>(a.light_depth_cue);
    proj.dqb = 0;
    const math::Projected p = math::project_vertex(proj, {r[2][0], r[2][1], r[2][2]});
    const std::int16_t cue = math::depth_cue(proj, p.h_over_sz);

    const std::int32_t sx = static_cast<std::int16_t>(p.sx + 1024);
    const std::int32_t sy = static_cast<std::int16_t>(p.sy + 1024);
    const std::int32_t depth = cue;

    std::int32_t level = ((sx * sy) + (sy * (depth >> 4))) >> 5;
    level -= 16;
    if (level < 0) {
        level = 0;
    }
    level += a.ambient >> 5;
    std::int32_t facing = (sx >> 1) + (depth >> 2);
    if (facing > 48) {
        facing = 48;
    }
    level += facing;
    if (a.light_level_max < level) {
        level = a.light_level_max;
    }
    out.amount = static_cast<std::uint8_t>(level);
    return out;
}

}  // namespace sh::world
