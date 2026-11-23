// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the named effect drawers (named_effects.h) read from and do to the running game, besides the engine classes
// they use directly: the frame the geometry state holds when a drawer starts and the one it
// leaves for the code after it, the view angle, the light falloff of the ground, textures and palette rows in
// video memory, sounds, the pad rumble and the player's hurt record. Implemented in
// src/engine/effects/game/effect_world.cpp.

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/app/loader/game_files.h"
#include "engine/effects/effect_prims.h"
#include "engine/game_state/session.h"
#include "engine/math/fixed_geometry.h"

namespace sh::effects::effect_world {

// Where a picture goes in video memory (the file queue's image place record: texture page, texel offset, palette).
using ImagePlace = ::sh::loader::ImagePlace;

// Slots of the world layer (the scene's ordering table).
inline constexpr std::int32_t kWorldSlots = 2048;

// The rotation and translation the game's geometry state holds now, and the frame a drawer leaves in it for the
// code that follows (the original's drawers left their last frame loaded); leave_rotation loads only the rotation.
// Engine state: src/engine/effects/game/effect_world.cpp.
math::Transform current_frame();
void leave_frame(const math::Transform& frame);
void leave_rotation(const math::Matrix3& rotation);

// Steps the game's packet cursor over the memory the original drawer's primitive took, so the drawers that follow
// write their packets where they did in the original.
void skip_packet(std::size_t bytes);

// The environment presets of gamedata/config/map_info.json as the game loaded them (21 entries).
const game::EnvironmentPreset* environment_presets();
// The water light zones of the map the world drawing was set up for (opaque; null when none).
void* map_water_zones();

// The yaw of the view (Q12 angle).
std::int32_t view_yaw();

// The world pass's ortho (left, right, top, bottom) and its display size; false while none was published.
bool world_ortho(float ortho[4], float display[2]);

// The skeleton record of the player (the bone frames the hierarchy walk takes).
const void* player_bones();
// The heading (Q12 angle) of a character record.
std::int32_t chara_heading(const void* chara);

// A texture of the loaded set by name: its page word and the place of its palette; false when it is not loaded.
bool texture_place(const std::string& name, std::uint16_t& page, std::uint16_t& palette_x, std::uint16_t& palette_y);
// Palette rows of 16 colours in video memory.
void read_palette_row(int x, int y, std::uint16_t out[16]);
void write_palette_rows(int x, int y, int rows, const std::uint16_t* data);

// Light falloff (0..255) of a ground point (Q8) of the model frame `model` facing `normal`. Loads the model light
// and the depth cue into the geometry state, as the game's lighting does.
std::uint8_t ground_light(const Vec3s& point, const Vec3s& normal, const math::Transform& model);

// Sounds: at a world point (Q12) with distance attenuation, or flat (mono, no attenuation).
void play_sound_at(std::int32_t sound, const Vec3& at, std::int32_t volume);
void play_sound_flat(std::int32_t sound, std::int32_t volume);
void rumble(std::int32_t pattern);

// The player is hit: the attack that hit him and damage added to what he takes this frame.
void hurt_player(std::int32_t attack, std::int32_t amount);
// The NPC in slot `slot` (0-based) is the character named `chara` (gamedata/chara).
bool npc_is(int slot, const std::string& chara);

}  // namespace sh::effects::effect_world
