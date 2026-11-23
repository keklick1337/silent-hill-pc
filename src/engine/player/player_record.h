// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Where the player's live state is stored, and the parts of the running game Player has to ask.
// Implemented over the game's records by the player's host; tests implement it
// over plain fields. Each action names what it does in the game's terms.

#pragma once

#include "engine/maps/map_rules.h"
#include "engine/player/player_vars.h"

#include <cstdint>

namespace sh::game {

// One entry of the saved inventory.
struct InventorySlot {
    int item = -1;  // run-time item number, -1 empty
    int count = 0;
};

// A clip of Harry's clip table (the playback of the slot `status` of his table).
enum class PlaybackHalf : std::uint8_t { Body, Upper };

// Ground under a point: height (Q12, +Y down), type (0: no data) and slope.
struct GroundSurface {
    std::int32_t height = 0;
    int type = 0;
    int tilt_x = 0, tilt_z = 0;
};

// The result of a move through the walls: the offset actually travelled and the ground reached.
struct MoveResult {
    std::int32_t dx = 0, dy = 0, dz = 0;
    GroundSurface ground;
};

// A row of the map's reaction clip table: the clip it starts (packed status), the status that triggers it, and
// the clip time a mid-reaction switch starts at (keyframes).
struct ReactionRow {
    int status = 0;
    int trigger = 0;
    int time = 0;
};

// A bone's local matrix: rotation (Q12, row-major) and translation (geometry units, 256 = 1 m).
struct BoneMatrix {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

class PlayerRecord {
public:
    virtual ~PlayerRecord() = default;

    virtual std::int64_t get(PlayerVar v) const = 0;
    virtual void set(PlayerVar v, std::int64_t value) = 0;

    // --- State resets and the save ---
    // The held weapon model goes back to none (no item in the hand).
    virtual void reset_held_weapon_model() = 0;
    // The damage record of this tick is cleared (amount, direction, kind).
    virtual void clear_damage() = 0;
    // Position, rotation and health come from the running save.
    virtual void read_from_save() = 0;
    // Both halves of the player's model are Harry's.
    virtual void assign_harry_models() = 0;
    // The save's player block back to a new game: 8 inventory slots all empty, full health, nothing equipped,
    // timers and statistics zero.
    virtual void reset_save_player() = 0;
    // Inventory of the save: its slot count, a slot, the equipped item (run-time number) and the item the
    // inventory screen shows as equipped.
    virtual int inventory_slot_count() const = 0;
    virtual void set_inventory_slot_count(int count) = 0;
    virtual InventorySlot inventory_slot(int idx) const = 0;
    virtual int equipped_item() const = 0;
    virtual void set_inventory_cursor_item(int item) = 0;
    // Item number of the ammo of the weapon item `item` (the item number arithmetic of the inventory).
    virtual int ammo_item_of(int item) const = 0;
    // The weapon carried as item `item`: its packed tap attack (-1: the item is no weapon); a gun or not.
    virtual int weapon_attack_of_item(int item, bool* gun) const = 0;
    virtual int max_health() const = 0;

    // --- Clips ---
    // The map's own clips go into Harry's table: entries from `src` of the map's table into `dst` of Harry's,
    // `count` of them (-1: up to the map table's end mark).
    virtual void install_map_clips(int dst, int src, int count) = 0;
    // Plays Harry's table entry `status` on the given half of the skeleton (the game's clip playback).
    virtual void play_clip(PlaybackHalf half, int status) = 0;
    // Index of Harry's clip by its name in his data (gamedata/chara/Harry/character.json "anims"), -1 unknown.
    virtual int clip_index(const char* name) const = 0;
    // First / last keyframe of Harry's table entry `status`; and the entry it hands over to.
    virtual int clip_start(int status) const = 0;
    virtual int clip_end(int status) const = 0;
    virtual int clip_link(int status) const = 0;

    // --- Weapon equip block ---
    // Installs the equip block of the weapon (the rig of gamedata/weapons): info row, hand reach, clips into
    // Harry's slots 56..75, the ten keyframe records. False (nothing installed) for no rig.
    virtual bool install_weapon_rig(int weapon_attack) = 0;
    // Empty-handed: no attack sounds, no attack clips.
    virtual void clear_weapon_info() = 0;
    // Reads the weapon's animation file into the weapon animation buffer and waits for it.
    virtual void load_weapon_animation(int weapon_attack) = 0;
    // The weapon's number name (gamedata/weapons/<Name>.json) for its packed attack; empty when none.
    virtual const char* weapon_name(int weapon_attack) const = 0;
    // A mod's weapon packs its attacks into rows past the original's table (sh::ids::mod_attack_weapon): the
    // weapon number owning the row, -1 for the original's packed attacks (decoded by their arithmetic).
    virtual int mod_attack_weapon(int weapon_attack) const {
        (void)weapon_attack;
        return -1;
    }
    // Whether such a row belongs to a gun (the weapon's "class").
    virtual bool mod_attack_gun(int weapon_attack) const {
        (void)weapon_attack;
        return false;
    }
    // The game's sound command (equip sounds).
    virtual void sound_command(int command) = 0;
    // Attaches the held item to Harry's bone (mesh variant, joint).
    virtual void attach_held_item(int variant, int joint) = 0;

    // --- The tick's other owners ---
    // Damage that landed since the last tick (the reactions, gamedata/chara/Harry/damage.lua).
    virtual void receive_damage() = 0;
    // Harry's behaviour in Lua (gamedata/chara/Harry): the controller turning the pad into the controls; the
    // legs and their footsteps; the upper body and combat; the reaction states (rest_idle / rest_state answer
    // whether they took the tick), with their bookkeeping before and after.
    virtual void run_controller() = 0;
    virtual void run_legs() = 0;
    virtual void run_footsteps() = 0;
    virtual void run_upper_body() = 0;
    virtual void rest_before() = 0;
    virtual bool rest_idle() = 0;
    virtual bool rest_state() = 0;
    virtual void rest_after() = 0;
    // The game clock of the tick (play time); the map's own per-tick player hooks (MapRules::player_updates).
    virtual void timer_update() = 0;
    virtual void map_player_tick() = 0;
    // While a scene has control: the scene's own control and move of Harry (gamedata/chara/Harry/scripted.lua).
    virtual void scripted_control() = 0;
    virtual void scripted_move() = 0;
    // The debug keyframe inspector (it may pose the whole body itself).
    virtual void keyframe_inspector() = 0;

    // --- World ---
    // The rules of the map Harry is on (its overlay.json: reaction rows, floor push, level rooms, per-tick hooks).
    virtual const sh::maps::MapRules& map_rules() const = 0;
    virtual GroundSurface ground_at(std::int32_t x, std::int32_t z) = 0;
    // Moves Harry's body by (dx, dy, dz) through the walls (the result is also the collision state other code reads
    // as FloorHeight / FloorGroundType); `walls` false moves straight and reports `here` as the ground.
    virtual MoveResult move_through_walls(std::int32_t dx, std::int32_t dy, std::int32_t dz, bool walls,
                                          const GroundSurface& here) = 0;
    // The pushing floor of the map takes back the part of a push the walls refused.
    virtual void push_feedback(std::int32_t dx, std::int32_t dz) = 0;
    // A character pushed Harry since the last call (an engine-spawned Split Head off its map, whose floor does not
    // push): its push applies like the pushing floor's. Taking it clears it.
    virtual bool character_push_take() = 0;
    // A sideways move by (dx, dz) through the walls that leaves the tick's collision state alone.
    virtual MoveResult slide(std::int32_t dx, std::int32_t dz) = 0;
    // The map's speed zone at (x, z): 0 slow, 1 normal, 2 fast.
    virtual int speed_zone(std::int32_t x, std::int32_t z) const = 0;
    // The first keyframe of the equipped weapon's clip record `i` (the ten records of the rig).
    virtual int weapon_clip_start(int i) const = 0;
    // The PC's movement keys held (bit 0 forward, 1 left, 2 back, 3 right), the fixed camera's position and look
    // yaw, the host's clock (ms).
    virtual unsigned movement_keys() const = 0;
    virtual void view_camera(std::int32_t position[3], std::int32_t* yaw) const = 0;
    virtual std::uint32_t time_ms() const = 0;
    // A phantom floor above the feet was refused (debug counter).
    virtual void count_phantom_floor() = 0;
    // The map's reaction clip table (Harry's clips of grabs, damage, scripted poses): rows to scan, a row, and
    // whether a spawned enemy brought the row.
    virtual int reaction_row_count() const = 0;
    virtual ReactionRow reaction_row(int row) const = 0;
    virtual bool reaction_row_imported(int row) const = 0;
    // GAMEPLAY CHANGE stick_radius: the stick push is the radius of both axes, not the vertical axis.
    virtual bool stick_radius() const { return false; }

    // --- Pose ---
    // Rotates bone `bone` of the pose by the three angles (Q12), the game's bone rotation call; `refresh` marks
    // its world matrix stale so readers this tick recompute it.
    virtual void rotate_bone(int bone, int rot_z, int rot_x, int rot_y, bool refresh) = 0;
    virtual BoneMatrix bone_local(int bone) const = 0;
    virtual void set_bone_local(int bone, const BoneMatrix& m, bool refresh) = 0;
    // Samples keyframes (kf0, kf1 at alpha, Q12) of Harry's animation into the pose (bones not disabled), or into
    // a scratch skeleton read back with scratch_bone.
    virtual void pose_keyframes(int kf0, int kf1, int alpha) = 0;
    virtual void pose_scratch(int kf0, int kf1, int alpha) = 0;
    virtual BoneMatrix scratch_bone(int bone) const = 0;
    // The animation file: bone count, keyframe count, and whether bone `bone` has keyed translation / rotation.
    virtual int anim_bone_count() const = 0;
    virtual int anim_keyframe_count() const = 0;
    virtual bool bone_keys_translation(int bone) const = 0;
    virtual bool bone_keys_rotation(int bone) const = 0;
    // The root bone follows the body (geometry units).
    virtual void set_root_translation(std::int32_t x, std::int32_t y, std::int32_t z) = 0;
};

}  // namespace sh::game
