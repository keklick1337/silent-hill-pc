// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The map's lights.json (engine/effects/map_lights.h) described into the frame (scene::Frame::lights and sun) once
// per frame: flag conditions, the bone a light follows and the effect it waits for are resolved here, so the renderer
// only sees lights in world units. Nothing here reads or changes the simulation's state beyond looking at it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/camera/camera_types.h"
#include "engine/camera/view_system.h"
#include "engine/effects/map_lighting_config.h"
#include "engine/effects/map_lights.h"
#include "engine/effects/named_effects_host.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"
#include "engine/world/carried_lights.h"
#include "render/gpu/render_state.h"
#include "render/scene/scene.h"

namespace {

namespace fx = sh::effects;

// Metres to the world units of the scene (PS1 axes, 256 per metre).
constexpr float kUnitsPerMetre = 256.0f;
// A light in Harry's hand: its shadow rays stop this short of it, or the fingers and the arm around it would hide it
// from everything (the flashlight's shadow rays stop short of its lens the same way).
constexpr float kHeldNearClip = 0.2f * kUnitsPerMetre;

// The shadow map of a held light (lights.json "shadow_map") looks from the flame towards the body of the one who
// holds it, tipped this far below the horizontal: his shadow falls behind him on the ground and up the walls, and
// the wide frustum (the renderer's) still takes both.
constexpr float kHeldShadowTilt = 30.0f * 3.14159265f / 180.0f;

// A fire whose effect drew no particle this frame keeps its last light this many frames: the effects draw on the
// frames of a game update, and the frames presented between two updates show the same fire.
constexpr int kFireHoldFrames = 4;

struct Fire {
    fx::FireSample last;
    int held = 0;
};

struct FireData {
    std::uint32_t epoch = sh::mods::kNeverRead;
    fx::FireLights lights;
    std::array<Fire, std::size(fx::kFireEffects)> state{};
    std::uint32_t frame = 0;
};

FireData& fires() {
    static FireData d;
    if (sh::mods::data_stale(d.epoch)) {
        std::string source;
        auto r = fx::load_fire_lights(fx::named::gamedata_dir(), fx::named::mods_dir(), &source);
        d.lights = r ? std::move(r.value()) : fx::FireLights{};
        if (!r) SH_ERR("[FX] %s - fires give no light", r.error().c_str());
    }
    return d;
}

// The level of a light that follows an effect. The original redraws a hand flame with a new random brightness every
// frame and skips frames; a light copying that strobes. So the effect only says whether the flame burns (and its
// slow mean brightness), the light fades in and out with it, and the flicker is a smooth noise of its own: a few
// sines of unrelated rates, which a flame's light resembles more than per-frame jumps.
struct FollowedLevel {
    int map = -2;
    std::vector<float> on;      // 0..1, eased presence of the flame
    std::vector<float> mean;    // eased brightness of the drawn flame
    std::vector<float> unseen;  // seconds since the effect was last drawn
};

float ease_followed(std::size_t index, int map, std::optional<float> shown, float flicker, float dt, float time) {
    static FollowedLevel s;
    constexpr float kFadeIn = 0.18f, kFadeOut = 0.30f;  // time constants of the flame's presence, seconds
    constexpr float kMean = 0.40f;                      // time constant of its brightness
    constexpr float kHold = 0.20f;                      // a flame missing for less than this still burns
    if (s.map != map) {
        s = FollowedLevel{};
        s.map = map;
    }
    if (s.on.size() <= index) {
        s.on.resize(index + 1, 0.0f);
        s.mean.resize(index + 1, 1.0f);
        s.unseen.resize(index + 1, kHold);
    }
    const auto ease = [dt](float& v, float target, float tau) { v += (target - v) * (1.0f - std::exp(-dt / tau)); };
    if (shown) {
        s.unseen[index] = 0.0f;
        ease(s.mean[index], *shown, kMean);
    } else {
        s.unseen[index] += dt;
    }
    const bool burning = s.unseen[index] < kHold;
    ease(s.on[index], burning ? 1.0f : 0.0f, burning ? kFadeIn : kFadeOut);
    const float t = time + static_cast<float>(index) * 7.31f;
    const float wave = 0.5f + 0.5f * (0.5f * std::sin(t * 2.0f * 3.14159f * 2.3f) +
                                      0.3f * std::sin(t * 2.0f * 3.14159f * 4.1f + 1.7f) +
                                      0.2f * std::sin(t * 2.0f * 3.14159f * 6.7f + 4.2f));
    const float brightness = 1.0f - flicker + flicker * (0.5f * s.mean[index] + 0.5f * wave);
    return s.on[index] * brightness;
}

// A render-side noise in 0..1 for the flicker of a fire: never the game's random numbers.
float flicker_noise(std::uint32_t frame, std::size_t fire) {
    std::uint32_t h = frame * 2654435761u + static_cast<std::uint32_t>(fire) * 40503u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

struct Loaded {
    int map = -2;
    std::uint32_t epoch = sh::mods::kNeverRead;
    fx::MapLights lights;
    fx::GlowLights glows;  // the map's own or the default
};

Loaded& loaded() {
    static Loaded l;
    return l;
}

bool flag_set(const char* name) {
    const int f = sh::ids::flag_number(name);
    return f >= 0 && sh::game_state::event_flag(f);
}

bool flags_hold(const std::vector<std::string>& set, const std::vector<std::string>& clear,
                const std::vector<std::vector<std::string>>& any_of) {
    for (const std::string& n : set) {
        if (!flag_set(n.c_str())) return false;
    }
    for (const std::string& n : clear) {
        if (flag_set(n.c_str())) return false;
    }
    for (const std::vector<std::string>& group : any_of) {
        bool one = false;
        for (const std::string& n : group) one = one || (n[0] == '!' ? !flag_set(n.c_str() + 1) : flag_set(n.c_str()));
        if (!one) return false;
    }
    return true;
}

const fx::MapLights& lights_of(int map) {
    Loaded& l = loaded();
    if (l.map == map && !sh::mods::data_stale(l.epoch)) return l.lights;
    l.map = map;
    l.lights = {};
    l.glows = {};
    const char* name = sh::game_state::map_name(map);
    if (name == nullptr) return l.lights;
    std::string source;
    auto r = fx::load_map_lights(fx::named::gamedata_dir(), fx::named::mods_dir(), name, &source);
    if (!r) {
        SH_ERR("[FX] %s - the map has no lights", r.error().c_str());
        return l.lights;
    }
    l.lights = std::move(r.value());
    if (l.lights.glows) {
        l.glows = *l.lights.glows;
    } else {
        std::string glow_source;
        auto g = fx::load_lamp_lights(fx::named::gamedata_dir(), fx::named::mods_dir(), &glow_source);
        if (g) {
            l.glows = g.value();
        } else {
            SH_ERR("[FX] %s - lamp glows give no light", g.error().c_str());
            l.glows = {};
        }
    }
    if (!l.lights.lights.empty() || l.lights.sun)
        SH_LOG("[FX] %zu map lights%s from %s", l.lights.lights.size(), l.lights.sun ? " and a sun" : "",
               source.c_str());
    return l.lights;
}

// The axis of a held light's shadow map: from the flame through the vertical axis of Harry's body, tipped down.
// Straight down when the flame is right above that axis.
std::array<float, 3> held_shadow_axis(const std::array<float, 3>& flame) {
    const sh::entities::Vec3 at = sh::game_state::chara_position(0);
    // Q12 metres to world units.
    const float dx = static_cast<float>(at.x) / 4096.0f * kUnitsPerMetre - flame[0];
    const float dz = static_cast<float>(at.z) / 4096.0f * kUnitsPerMetre - flame[2];
    const float h = std::sqrt(dx * dx + dz * dz);
    if (h < 1.0f) return {0.0f, 1.0f, 0.0f};
    const float c = std::cos(kHeldShadowTilt), s = std::sin(kHeldShadowTilt);
    return {dx / h * c, s, dz / h * c};
}

// A point of a bone (offset in metres in the bone's space) in world units; `axis`, when given, is turned from the
// bone's space into the world's.
std::array<float, 3> on_frame(sh::camera::FrameRecord* frame, const std::array<float, 3>& offset,
                              std::array<float, 3>* axis) {
    const sh::camera::Matrix m = sh::camera::frame_world(frame);
    std::array<float, 3> out{};
    for (int r = 0; r < 3; ++r) {
        float v = static_cast<float>(m.t[r]);
        float a = 0.0f;
        for (int c = 0; c < 3; ++c) {
            v += static_cast<float>(m.m[r][c]) / 4096.0f * offset[static_cast<std::size_t>(c)] * kUnitsPerMetre;
            if (axis) a += static_cast<float>(m.m[r][c]) / 4096.0f * (*axis)[static_cast<std::size_t>(c)];
        }
        out[static_cast<std::size_t>(r)] = v;
        if (axis) (*axis)[static_cast<std::size_t>(r)] = a;
    }
    return out;
}

// A point of a bone of Harry.
std::array<float, 3> on_bone(int bone, const std::array<float, 3>& offset, std::array<float, 3>* axis) {
    auto& bones = sh::game::session().system.characters.player_bones;
    return on_frame(reinterpret_cast<sh::camera::FrameRecord*>(&bones[bone]), offset, axis);
}

// The bones the character in `slot` is posed on and how many they are: the player's own, another character's from
// the animation slot of its type. Null while the slot is empty or its type has no animations in.
sh::camera::FrameRecord* bones_of_slot(int slot, int* count) {
    if (slot == 0) {
        auto& bones = sh::game::session().system.characters.player_bones;
        *count = static_cast<int>(std::size(bones));
        return reinterpret_cast<sh::camera::FrameRecord*>(&bones[0]);
    }
    const int type = sh::game_state::chara_id(slot);
    if (type <= 0) return nullptr;
    const sh::gameflow::AnimSlotTable& table = sh::gameflow::anim_slot_table();
    const int index = table.chara_slot_of(type);
    if (!table.has_slot(index)) return nullptr;
    const sh::gameflow::AnimSlot& held = table.slots[static_cast<std::size_t>(index)];
    if (held.bone_coords == nullptr || held.active_header == nullptr || held.active_chara != type) return nullptr;
    // The animation file's own bone count (its header), as the slot logic reads it.
    *count = static_cast<const std::uint8_t*>(held.active_header)[6];
    return static_cast<sh::camera::FrameRecord*>(held.bone_coords);
}

// The lights the scripts gave characters to carry (sh.light.carry), each at its bone as the frame poses it.
void describe_carried_lights(sh::scene::Frame& f) {
    for (const sh::world::CarriedLight& l : sh::world::carried_lights()) {
        int count = 0;
        sh::camera::FrameRecord* bones = bones_of_slot(l.slot, &count);
        const int bone = l.bone >= 0 ? l.bone : fx::harry_bone_index(l.bone_name);
        if (bones == nullptr || bone < 0 || bone >= count) continue;
        sh::scene::FrameLight out;
        std::array<float, 3> axis = l.direction;
        const bool spot = axis[0] != 0.0f || axis[1] != 0.0f || axis[2] != 0.0f;
        out.position = on_frame(bones + bone, l.position, spot ? &axis : nullptr);
        if (spot) {
            const float len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            if (len <= 0.0f) continue;
            for (float& a : axis) a /= len;
            out.direction = axis;
            out.shadow_axis = axis;
            out.cos_inner = std::cos(l.cone_inner * 3.14159265f / 180.0f);
            out.cos_outer = std::cos(l.cone_outer * 3.14159265f / 180.0f);
        }
        for (std::size_t i = 0; i < 3; ++i) out.color[i] = l.color[i] * l.intensity;
        out.range = l.range * kUnitsPerMetre;
        out.radius = l.radius * kUnitsPerMetre;
        out.near_clip = kHeldNearClip;
        out.casts_shadows = l.casts_shadows;
        out.gi = l.gi;
        out.carried = true;
        f.lights.push_back(out);
    }
}

}  // namespace

namespace sh::effects {

void describe_map_lights() {
    sh::scene::Frame& f = sh::scene::current_frame();
    f.lights.clear();
    f.sun = {};
    const int map = sh::game_state::current_map();
    const MapLights& data = lights_of(map);
    const bool extra = sh::render::graphics().extra_lights != 0;
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.1f);
    last = now;
    static float time = 0.0f;
    time = std::fmod(time + dt, 1000.0f);
    for (std::size_t li = 0; li < data.lights.size(); ++li) {
        const MapLight& l = data.lights[li];
        if (l.extra && !extra) continue;
        if (!flags_hold(l.flags_set, l.flags_clear, l.any_of)) continue;
        float level = 1.0f;
        if (!l.while_effect.empty()) {
            level = ease_followed(li, map, effect_brightness(l.while_effect), l.flicker, dt, time);
            if (level < 0.01f) continue;
        }
        sh::scene::FrameLight out;
        std::array<float, 3> axis = l.direction;
        if (l.bone >= 0) {
            out.position = on_bone(l.bone, l.position, l.spot ? &axis : nullptr);
            out.near_clip = kHeldNearClip;
        } else {
            for (std::size_t i = 0; i < 3; ++i) out.position[i] = l.position[i] * kUnitsPerMetre;
        }
        if (l.spot) {
            out.direction = axis;
            out.cos_inner = std::cos(l.cone_inner * 3.14159265f / 180.0f);
            out.cos_outer = std::cos(l.cone_outer * 3.14159265f / 180.0f);
        }
        for (std::size_t i = 0; i < 3; ++i) out.color[i] = l.color[i] * l.intensity * level;
        out.range = l.range * kUnitsPerMetre;
        out.radius = l.radius * kUnitsPerMetre;
        out.casts_shadows = l.casts_shadows;
        out.shadow_map = l.shadow_map;
        if (l.spot)
            out.shadow_axis = axis;
        else if (l.bone >= 0)
            out.shadow_axis = held_shadow_axis(out.position);
        out.gi = l.gi;
        out.extra = l.extra;
        f.lights.push_back(out);
    }
    if (data.sun && flags_hold(data.sun->flags_set, data.sun->flags_clear, data.sun->any_of)) {
        const MapSun& s = *data.sun;
        f.sun.on = true;
        f.sun.direction = s.direction;
        for (std::size_t i = 0; i < 3; ++i) {
            f.sun.color[i] = s.color[i] * s.intensity;
            f.sun.sky[i] = s.sky_color[i] * s.sky_intensity;
        }
        f.sun.radius = s.angular_radius * 3.14159265f / 180.0f;
        for (const MapCeiling& c : s.ceilings)
            f.sun.ceilings.push_back({c.rect[0] * kUnitsPerMetre, c.rect[1] * kUnitsPerMetre, c.rect[2] * kUnitsPerMetre,
                                      c.rect[3] * kUnitsPerMetre, c.height * kUnitsPerMetre});
    }
    describe_carried_lights(f);
    for (const auto& [key, l] : effect_lights_shown()) {
        sh::scene::FrameLight out;
        out.position = l.position;
        out.color = l.color;
        out.range = l.range * kUnitsPerMetre;
        out.radius = l.radius * kUnitsPerMetre;
        out.near_clip = l.near_clip * kUnitsPerMetre;
        out.casts_shadows = l.casts_shadows;
        out.gi = l.gi;
        out.extra = true;  // an effect's own light: shown by every renderer, like the hand-placed lamps
        f.lights.push_back(out);
    }
    FireData& fd = fires();
    ++fd.frame;
    for (std::size_t i = 0; i < std::size(fx::kFireEffects); ++i) {
        fx::FireSample s = fx::take_fire_particles(fx::kFireEffects[i]);
        Fire& state = fd.state[i];
        if (s.amount > 0.0f) {
            state.last = s;
            state.held = 0;
        } else if (state.last.amount > 0.0f && ++state.held <= kFireHoldFrames) {
            s = state.last;
        } else {
            state.last = {};
            continue;
        }
        const fx::FireLight* light = nullptr;
        for (const auto& [name, l] : fd.lights)
            if (name == fx::kFireEffects[i]) light = &l;
        if (light == nullptr) continue;
        const float noise = flicker_noise(fd.frame, i);
        const float level = std::min(s.amount / light->full, 2.0f) * (1.0f - light->flicker * noise) * light->intensity;
        if (level <= 0.0f) continue;
        sh::scene::FrameLight out;
        out.position = {s.position[0], s.position[1] + light->lift * kUnitsPerMetre, s.position[2]};
        for (std::size_t k = 0; k < 3; ++k) out.color[k] = light->color[k] * level;
        out.range = light->range * kUnitsPerMetre;
        out.radius = light->radius * kUnitsPerMetre;
        out.casts_shadows = light->casts_shadows;
        out.gi = light->gi;
        out.extra = true;
        f.lights.push_back(out);
    }
    const fx::GlowLights& glows = loaded().glows;
    // Street and ceiling lamps shine only while no sun does: by day the original's glows are faint halos.
    if (glows.on && !f.sun.on) {
        for (const std::array<float, 4>& g : f.lamp_glows) {
            sh::scene::FrameLight out;
            out.position = {g[0], g[1] + glows.drop * kUnitsPerMetre, g[2]};
            for (std::size_t i = 0; i < 3; ++i) out.color[i] = glows.color[i] * glows.intensity;
            out.range = glows.range * kUnitsPerMetre;
            out.radius = glows.radius * kUnitsPerMetre;
            out.casts_shadows = glows.casts_shadows;
            out.gi = glows.gi;
            f.lights.push_back(out);
        }
    }
    clear_effects_shown();
}

}  // namespace sh::effects
