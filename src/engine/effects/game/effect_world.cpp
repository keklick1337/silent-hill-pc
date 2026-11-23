// SPDX-License-Identifier: GPL-3.0-or-later
// The parts of effect_world.h over engine state: the frame loaded in the geometry state, the view angle, the
// world pass's ortho, the session's skeleton and character records, sounds and the pad rumble.

#include <cstdint>
#include <string>

#include "engine/math/precise_capture.h"
#include "engine/app/map_info_tables.h"
#include "engine/camera/camera_types.h"
#include "engine/camera/view_system.h"
#include "engine/effects/effect_world.h"
#include "engine/game_state/session.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/sh_maps.h"
#include "engine/math/geometry_state.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "render/gpu/renderer.h"
#include "render/gpu/render_state.h"

extern "C" {
extern unsigned char* GsOUT_PACKET_P;  // the game's packet cursor
// Engine entry points of the sound placement and the pad rumble (audio/game, gameflow/game).
void Sfx_WithFlagsPlay(int sound, const std::int32_t* at, std::int32_t volume, std::int32_t flags);
void Vibration_Play(std::int32_t pattern);
}

namespace ew = sh::effects::effect_world;

sh::math::Transform ew::current_frame() { return sh::math::geometry().view(); }

void ew::leave_frame(const sh::math::Transform& t) {
    sh::math::geometry().set_rotation(t.rot);
    sh::math::geometry().set_translation(t.trans);
}

void ew::leave_rotation(const sh::math::Matrix3& rot) {
    sh::math::geometry().set_rotation(rot);
    // The rotation came from no matrix the precise path knows: its exact copy is dropped, as loading a fresh
    // matrix did.
    sh::math::Matrix3 copy = rot;
    sh::math::capture::rotation_loaded(&copy);
}

void ew::skip_packet(std::size_t bytes) { GsOUT_PACKET_P += bytes; }

std::int32_t ew::view_yaw() { return sh::camera::view_system().angles().y; }

bool ew::world_ortho(float ortho[4], float display[2]) {
    for (int i = 0; i < 4; ++i) ortho[i] = sh::render::published().world_ortho[i];
    display[0] = sh::render::published().world_disp[0];
    display[1] = sh::render::published().world_disp[1];
    return sh::render::published().world_ortho_valid != 0;
}

const void* ew::player_bones() { return sh::game::session().system.characters.player_bones; }

std::int32_t ew::chara_heading(const void* chara) {
    return chara != nullptr ? static_cast<const sh::entities::CharacterRecord*>(chara)->rotation.y : 0;
}

namespace {
constexpr std::int32_t kSoundMono = 1 << 0;
constexpr std::int32_t kSoundNoDistance = 1 << 1;
}  // namespace

void ew::play_sound_at(std::int32_t sound, const sh::effects::Vec3& at, std::int32_t volume) {
    const std::int32_t pos[3] = {at.x, at.y, at.z};
    Sfx_WithFlagsPlay(sound, pos, volume, 0);
}

void ew::play_sound_flat(std::int32_t sound, std::int32_t volume) {
    Sfx_WithFlagsPlay(sound, nullptr, volume, kSoundMono | kSoundNoDistance);
}

void ew::rumble(std::int32_t pattern) { Vibration_Play(pattern); }

void ew::hurt_player(std::int32_t attack, std::int32_t amount) {
    sh::entities::CharacterRecord& player = sh::game::session().system.characters.player;
    player.attack_received = attack;
    player.damage.amount += amount;
}

bool ew::npc_is(int slot, const std::string& chara) {
    const int id = ShMaps_CharaNumberOf(chara.c_str());
    return id >= 0 && slot >= 0 && slot < sh::entities::kNpcSlotCount &&
           sh::game::session().system.characters.npcs[slot].model.chara_id == id;
}

// ---- the environment presets, the map's water zones, textures and video memory, the ground light ----

extern "C" {
// The light falloff of a ground point: point and normal are three Q8/Q12
// shorts, the model a matrix record.
std::uint8_t WorldEnv_FadeAmountGet(const void* point, const void* normal, const void* model);
}

const sh::game::EnvironmentPreset* ew::environment_presets() { return sh::app::environment_presets(); }

void* ew::map_water_zones() {
    const sh::app::MapTypeRecord* info = sh::app::map_type_record(sh::maps::map_runtime().map_type());
    return info != nullptr ? const_cast<sh::app::WaterZoneRecord*>(info->water_zones) : nullptr;
}

bool ew::texture_place(const std::string& name, std::uint16_t& page, std::uint16_t& palette_x,
                       std::uint16_t& palette_y) {
    return sh::world::stream::texture_place(name, page, palette_x, palette_y);
}

void ew::read_palette_row(int x, int y, std::uint16_t out[16]) {
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->store_image({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), 16, 1}, out);
    }
}

void ew::write_palette_rows(int x, int y, int rows, const std::uint16_t* data) {
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->load_image({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), 16, static_cast<std::int16_t>(rows)},
                      data);
    }
}

std::uint8_t ew::ground_light(const sh::effects::Vec3s& point, const sh::effects::Vec3s& normal,
                              const sh::math::Transform& model) {
    const std::int16_t p[3] = {point.x, point.y, point.z};
    const std::int16_t n[3] = {normal.x, normal.y, normal.z};
    sh::camera::Matrix m;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m.m[r][c] = model.rot.m[r][c];
    }
    m.t[0] = model.trans.x;
    m.t[1] = model.trans.y;
    m.t[2] = model.trans.z;
    return WorldEnv_FadeAmountGet(p, n, &m);
}
