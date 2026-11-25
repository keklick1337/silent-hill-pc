// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The session of the running game: the screen and in-game state machine with
// their steps and frame counters, the player's combat record, the NPC bookkeeping, the light rig, the script
// camera angles, the world environment mix, the music mix, the process and system flags, the settings, the pads
// and the progress records. The engine owns this storage; sh::game::session() is the one instance.
//
// These are the engine's own classes: every value has a name and a type, and nothing reads them as bytes. What needs
// the console's byte layout converts by name: the memory card and demo records (engine/save/console_records.cpp);
// the replay's state dump hashes values field by field (engine/game_state/game_state_replay_probe.cpp).
//
// Units are the game's: positions Q12 metres (+Y down), angles Q12 turns, time Q12 seconds.

#pragma once

#include "engine/player/player_state.h"

#include <cstdint>

#include "engine/camera/view_system.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/progress_record.h"
#include "engine/game_state/raw_pad.h"
#include "engine/game_state/settings_record.h"

namespace sh::game {

// An 8-bit colour with a spare byte (alignment 1).
struct Rgbx {
    std::uint8_t r, g, b, x;
};
struct Vec3i {
    std::int32_t x, y, z;
};
struct Vec3s {
    std::int16_t x, y, z, w;
};

// The in-game state machine (the game's SysState) and the frame counters of both state machines.
struct SessionFlow {
    std::int32_t sys_state;
    // Steps of the in-game state (levels 0..2): sub-states or data of the current state.
    std::int32_t sys_steps[3];
    // A map message string is loaded and being shown.
    std::int32_t message_shown;
    // Frames since: [0] the screen changed, [1] the screen's step 0 changed, [2] the in-game state changed.
    std::int32_t frame_counters[3];
    // Data of in-game steps 0 and 1, cleared with their step (timers, fade alphas).
    std::int32_t sys_step_data[2];
    // CutsceneBorderState of the letterbox bars.
    std::int32_t cutscene_border;
};

// The player's weapon in hand.
struct PlayerCombat {
    Vec3i attack_position;
    // Packed attack: weapon in the low nibble's group, attack input type in the high one (the game's WEAPON_ATTACK).
    std::int32_t weapon_attack;
    std::int32_t ammo_loaded;
    std::int32_t ammo_total;
    // Inventory slot of the equipped weapon.
    std::int8_t weapon_slot;
    std::uint8_t aiming;
};

// NPC bookkeeping and the area transition marks.
struct SessionScene {
    // 1-based NPC id the NPC flags belong to.
    std::int32_t npc_flags_owner;
    std::int8_t loading_screen;
    // Flags of the event that started the current area transition.
    std::int8_t area_transition_flags;
    // Index of the door sound pair of the transition.
    std::int32_t sfx_pair;
    std::uint16_t chara_group_flags[4];
    // Bit per enemy spawn group: the group is spawned.
    std::int32_t spawned_groups;
    // Bit per NPC slot (the first 32).
    std::int32_t npc_flags;
};

// Process and system flags (the game's ProcessFlag / BgmStatusFlag / SysFlag bits).
struct SessionProcess {
    std::uint32_t process_flags;
    // -1 once the player is set up, 0 on an area change; nothing reads it.
    std::int32_t player_ready_mark;
    std::uint32_t music_status;
    std::uint32_t sys_flags;
};

// Particle effects of the area.
struct SessionParticles {
    // The two frames the particle effects attach to.
    sh::camera::FrameRecord anchors[2];
    std::int8_t density_applied;
    std::int8_t density_mode;
    std::uint8_t transition_ready;
    // Low nibble: a particle sound cue (nothing sets it); high nibble: the insect swarm mode.
    std::uint8_t cue_and_insects;
};

// A world environment preset: fog, light and tint (44 bytes).
struct EnvironmentPreset {
    std::uint8_t flags;
    std::uint8_t light_level_max;
    std::uint8_t light_mode;
    std::int16_t spot_light_intensity;
    std::int16_t world_light_intensity;
    std::int16_t world_tint[3];
    std::uint8_t fog_mode;
    std::int32_t fog_distance;
    Rgbx fog_color;
    std::uint8_t tint_light_overlap;
    Rgbx point_light_tint;
    Rgbx world_tint_color;  // subtractive
    Rgbx snow_color;
    Rgbx rain_color;
};

struct EnvironmentState {
    EnvironmentPreset preset;
    std::int16_t lens_flare_intensity;
    std::int16_t brightness_intensity;
    std::int32_t fog_distance;
};

// The gameplay side of the world environment: presets mixed by a transition and by the flashlight
// (index 0: flashlight off, 1: on). The drawn copy is sh::world::AtmosphereState.
struct EnvironmentMix {
    std::int32_t progress_type;
    const void* progress_value;
    std::int32_t progress_start;
    std::int32_t progress_end;
    std::int32_t visibility_distance;
    std::uint8_t flash_effect;
    std::uint8_t flashlight_on;
    std::uint8_t flashlight_unavailable;
    std::int16_t flashlight_intensity;
    EnvironmentState transition[2];
    EnvironmentState transition_to[2];
    EnvironmentState transition_from[2];
    EnvironmentState current;
};

// Music layer volumes and the gas weapons' engine sound levels.
struct SessionAudio {
    // Q12 per mix channel; the last one is the ducking timer.
    std::int16_t music_layer_volumes[9];
    std::int32_t gas_weapon_engine_level;
    std::int32_t gas_weapon_contact_level;
    std::int32_t rock_drill_run_level;
};

// The play state of the session, cleared whole on a warm reset (Session::clear_system).
struct SystemBlock {
    SessionFlow flow;
    PlayerCombat combat;
    sh::entities::CharacterSlots characters;
    SessionScene scene;
    SessionProcess process;
    SessionParticles particles;
    std::int32_t map_message_timer;
    // Low nibble: half-height glyphs; high nibble: "yes" is selected without a sound.
    std::uint8_t text_flags;
    std::uint8_t inventory_selected;
    std::uint8_t inventory_load_flags;
    // NPC slot the player targets (-1: none).
    std::int32_t target_npc;
    std::int32_t group_npcs[4];
    std::uint8_t player_match_anim;
    std::uint8_t player_stop_flags;
    // Light rig (often driven by a cutscene): the bone the light follows, its position, the lens flare bone.
    const void* light_bone;
    Vec3i light_position;
    const void* lens_flare_bone;
    Vec3s light_rotation;
    std::int16_t light_intensity;
    // Script camera.
    std::int16_t camera_angle_y;
    std::int16_t camera_angle_z;
    std::int32_t camera_radius_xz;
    std::int32_t camera_height;
    EnvironmentMix environment;
    SessionAudio audio;
};

struct PadSticks {
    std::int8_t right_x, right_y, left_x, left_y;
};

// A pad as the game reads it each frame.
struct PadState {
    RawPad raw;
    std::int32_t pulse_ticks;
    std::uint32_t held;
    std::uint32_t clicked;
    std::uint32_t released;
    std::uint32_t pulsed;
    std::uint32_t pulsed_menu;
    // Signed stick positions in analog mode; the deflection past the dead zone; the walking controller's steering
    // (four signed bytes in the stick order).
    PadSticks sticks;
    PadSticks sticks_past_dead_zone;
    PadSticks steer;
};

// The settings, pads, progress records and screen state of the session: kept across a warm reset.
struct GameBlock {
    Settings settings;
    PadState pads[2];
    // The progress records: the automatic copy a death returns to and the live one (progress_record.h).
    SaveRecord autosave;
    SaveRecord save;
    std::uint16_t screen_width;
    std::uint16_t screen_height;
    Rgbx clear_color;
    std::int32_t game_state_previous;
    std::int32_t game_state;
    // Steps of the top-level screen (levels 0..2): sub-states or data.
    std::int32_t game_steps[3];
    // Set when a demo starts; nothing reads them.
    std::int32_t demo_marks[2];
    std::int32_t map_anim_index;
    std::int32_t music_track;
    std::int32_t ambient_bank;
    RawPad raw_pad;
};

// Values of the game's state machines the engine names.
namespace sys_state {
constexpr std::int32_t kGameplay = 0;
constexpr std::int32_t kGameOver = 13;
constexpr std::int32_t kPaused = 14;
}  // namespace sys_state
namespace game_state {
constexpr std::int32_t kMainMenu = 7;
constexpr std::int32_t kMainLoadScreen = 10;
constexpr std::int32_t kInGame = 11;
constexpr std::int32_t kInventoryScreen = 14;
}  // namespace game_state
namespace sys_flag {
constexpr std::uint32_t kDemo = 1u << 1;
constexpr std::uint32_t kCutscene = 1u << 3;
constexpr std::uint32_t kNoEnemySpawn = 1u << 4;
constexpr std::uint32_t kSceneLock = 1u << 5;  // a scene (the cafe talk) holds the NPCs still and lets the world run under messages
constexpr std::uint32_t kOnCameraRail = 1u << 6;
constexpr std::uint32_t kMenu = 1u << 7;
constexpr std::uint32_t kWarmReset = 1u << 8;
constexpr std::uint32_t kLoading = 1u << 10;
}  // namespace sys_flag
// The letterbox bars' resting state (SessionFlow::cutscene_border).
constexpr std::int32_t kCutsceneBorderNone = 1;
namespace process_flag {
constexpr std::uint32_t kRoomTransition = 1u << 0;
constexpr std::uint32_t kOverlayTransition = 1u << 1;
constexpr std::uint32_t kNewGame = 1u << 2;
constexpr std::uint32_t kLoadSave = 1u << 3;
constexpr std::uint32_t kContinue = 1u << 4;
constexpr std::uint32_t kBootDemo = 1u << 5;
}  // namespace process_flag
namespace music_status {
constexpr std::uint32_t kPause = 1u << 0;
}  // namespace music_status

class Session {
public:
    SystemBlock system;
    GameBlock game;
    // The player's live state outside his character record (engine/player/player_state.h).
    PlayerState player;

    // In-game state machine.
    std::int32_t sys_state() const { return system.flow.sys_state; }
    // The state the next tick runs: steps, step data and the state's frame counter start over.
    void set_sys_state_next(std::int32_t state);
    // Step `level` (0, 1, anything else: 2) to `value`; the levels under it and their data start over.
    void set_sys_step(int level, std::int32_t value);
    // Step `level` advances by one; the levels under it and their data start over.
    void advance_sys_step(int level);

    // Top-level screen.
    std::int32_t game_state() const { return game.game_state; }
    // The screen the next tick runs: the previous one is remembered, its steps and frame counters start over and
    // the in-game state goes back to gameplay.
    void set_game_state_next(std::int32_t state);
    // Back to the previous screen (which becomes the one left), with the same restart as set_game_state_next.
    void return_to_previous_screen();
    // Screen step `level` (0..2) to `value`; the levels under it start over (and the step frame counter with 0).
    void set_game_step(int level, std::int32_t value);

    bool sys_flag(std::uint32_t flag) const { return (system.process.sys_flags & flag) != 0; }
    void set_sys_flag(std::uint32_t flag, bool on);
    bool process_flag(std::uint32_t flag) const { return (system.process.process_flags & flag) != 0; }
    bool music_status(std::uint32_t bit) const { return (system.process.music_status & bit) != 0; }
    bool cutscene_live() const {
        return sys_flag(sys_flag::kCutscene) || system.flow.cutscene_border != kCutsceneBorderNone;
    }

    // The whole system block back to zero (the game's play-state clear on a warm reset).
    void clear_system();
};

namespace detail {
// Static storage: the records' views bind to it before any code runs (only the player state has initial values).
inline Session g_session;
}  // namespace detail

inline Session& session() { return detail::g_session; }

}  // namespace sh::game
