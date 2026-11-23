// SPDX-License-Identifier: GPL-3.0-or-later
// The parts of effect_link.h over engine state: the packet cursor (owned here), the map effect flags, the
// session's particle anchor frames and the world-screen matrix.

#include "engine/effects/game/effect_link.h"

#include <cstring>

#include "engine/camera/camera_host.h"
#include "engine/camera/view_space.h"
#include "engine/effects/game/slot_hosts.h"
#include "engine/effects/map_effect_view.h"
#include "engine/game_state/session.h"
#include "render/game_view.h"
#include "engine/app/loader/picture_places.h"

extern "C" {
// The game's packet cursor: only the drawers' packet addresses (nothing reads packets); the frame start puts it
// at the packet area of the buffer being built.
unsigned char* GsOUT_PACKET_P = nullptr;
// The slot table's allocator (the map effect frame's host).
std::int32_t MapOverlay_SlotAlloc(std::int32_t kind);
// The effect picture places (src/engine/app/loader/picture_places.cpp).
}

namespace fg = sh::effects::game_link;
namespace fx = sh::effects;
namespace cam = sh::camera;

unsigned char*& fg::packet_cursor() { return GsOUT_PACKET_P; }

std::uint32_t fg::map_effect_flags() { return static_cast<std::uint8_t>(g_MapEffectFlags); }

void fg::set_map_effect_flags(std::uint32_t flags) { g_MapEffectFlags = static_cast<std::int8_t>(flags); }

namespace {

// A narrow word in the original (the weather setters' requests may read back truncated).
std::int8_t s_weather_light_mode = 0;

}  // namespace

int fg::weather_light_mode() { return s_weather_light_mode; }

void fg::set_weather_light_mode(int mode) { s_weather_light_mode = static_cast<std::int8_t>(mode); }

fx::effect_world::ImagePlace& fg::effect_image_place(EffectImagePlace place) {
    switch (place) {
        case EffectImagePlace::Fire: return sh::loader::picture_places().fire_effect;
        case EffectImagePlace::Blood: return sh::loader::picture_places().blood_effect;
        case EffectImagePlace::Sheet: break;
    }
    return sh::loader::picture_places().effect;
}

int fg::slot_alloc(std::uint8_t kind) {
    const std::int32_t slot = MapOverlay_SlotAlloc(kind);
    return slot < 0 ? -1 : slot;
}

namespace {

cam::FrameRecord& anchor(int index) { return sh::game::session().system.particles.anchors[index]; }

}  // namespace

void fg::reset_particle_anchor(int index) {
    cam::FrameRecord& f = anchor(index);
    std::memset(static_cast<void*>(&f), 0, sizeof(f));
    f.local.m[0][0] = 4096;
    f.local.m[1][1] = 4096;
    f.local.m[2][2] = 4096;
    f.world_current = 1;  // a root: the hierarchy walk stops at it
}

cam::Matrix fg::particle_anchor_view(int index, const std::int32_t translation[3]) {
    reset_particle_anchor(index);
    cam::FrameRecord& f = anchor(index);
    f.local.t[0] = translation[0];
    f.local.t[1] = translation[1];
    f.local.t[2] = translation[2];
    f.world_current = 0;
    const cam::Matrix world = cam::frame_world(&f);
    return cam::view_matrix(cam::view_system().world_screen(), world);
}

cam::Matrix& fg::world_screen_matrix() { return cam::camera_host().world_screen(); }
