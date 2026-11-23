// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The player: Harry's resets and set-up, the
// queries other systems ask him, the weapon he holds. Behaviour per tick lives in Lua (gamedata/chara/Harry);
// this class is the engine side every caller goes through. It reads and writes the player's state only through
// PlayerRecord (player_record.h), never through the game's records.

#pragma once

#include "engine/player/player_record.h"

#include <cstdint>
#include <string_view>

namespace sh::game {

class Player {
public:
    explicit Player(PlayerRecord& record) : r_(record) {}

    PlayerRecord& record() { return r_; }
    std::int64_t get(PlayerVar v) const { return r_.get(v); }
    void set(PlayerVar v, std::int64_t value) { r_.set(v, value); }

    // --- Set-up ---
    // The save's player block back to a new game.
    void reset_save() { r_.reset_save_player(); }
    // Harry from the running save at a map entry or a load: his record, models, collision, the equipped weapon
    // with its ammo, the stun limit of the difficulty, the clamps of slot count and health.
    void init_from_save();
    // A map was entered: its own clips go into Harry's table, collision shape and tiredness reset, then the
    // whole state (reset_state). `put_out_match`: the map starts with the match out (the first street).
    void enter_map(bool put_out_match);
    // The map's eight extra clips (map table 16..23) into Harry's slots 92..99.
    void append_map_clips() { r_.install_map_clips(kMapExtraClipSlot, kMapExtraClipSource, kMapExtraClipCount); }
    // Every body state, timer, control and link back to rest (respawn, scripted resets).
    void reset_state();
    // A whole-body state change: both halves' behaviour steps restart, the half states go back to none.
    void enter_state(int state);
    // No control pressed this tick; the walk/run blend ends.
    void clear_controls();

    // The map's reaction clip for `trigger` (its row in the map's table) on both halves, each half only when its
    // behaviour step is still 0; `restart` instead jumps both into the row's playing clip at the row's time (the
    // mid-reaction switch). Nothing happens while the upper body's reaction already runs.
    void start_reaction_clip(int trigger);
    void switch_reaction_clip(int trigger);

    // --- Queries ---
    // Not free to act: dying, aiming, kicking or stomping, or in a reaction (grabs, damage, falls, get-ups).
    bool busy() const;
    // An attack is running (a swing, a shot, a kick or a stomp).
    bool attacking() const;
    // Dying; and damage switched off (the caller gets the dying flag back, as the game's call did).
    bool dead() const { return get(PlayerVar::Dead) != 0; }
    void set_damage_disabled(bool off) { set(PlayerVar::DamageDisabled, off ? 1 : 0); }
    // Time the current attack has run (its hit window counts on it).
    std::int32_t attack_time() const { return static_cast<std::int32_t>(get(PlayerVar::AttackTime)); }
    // First keyframe of the legs' current clip.
    int body_clip_start() const { return r_.clip_start(static_cast<int>(get(PlayerVar::BodyAnimStatus))); }
    // The upper body leans a hair to the side: scripted turns start the head-and-torso lean from here.
    void nudge_flex_lean() { set(PlayerVar::FlexRotY, kFlexNudge); }

    // --- Weapon ---
    // The equipped weapon's equip block into Harry (sounds, clips, reach) and, on a change, its ready sound and
    // its animation file. Clears the aim target.
    void update_weapon();
    // The ready sound of a weapon (by its packed tap attack); other attacks are silent.
    void play_ready_sound(int weapon_attack);
    // A packed attack is a gun's: a mod's row by its weapon's class, else the original's rule (handgun's tap on).
    bool gun_attack(std::int64_t weapon_attack) const;

    // --- The tick (player_tick.cpp) ---
    // One game tick of Harry: damage, behaviour (or the scene's control), the move, both halves' clips and the
    // pose layers (the classic aim lean, the alternate cameras' aim hold and rear look, debug inspection).
    void tick();
    // The move of this tick: the legs' speed and heading through the walls, the ground under the feet, falls off
    // ledges, the run's free distance and the results screen's totals.
    void integrate();
    // Both halves of the skeleton play their clips (legs, then upper body), the held item on its bone, the
    // walk/run hand-over of the legs.
    void animate();
    // The classic camera's upper-body lean toward the aim, the head turns of the scripted look-arounds and the
    // match arm.
    void lean_classic();
    // Harry's behaviour of the tick (the controller, the legs, the upper body, the reaction states), or, under
    // the PC's alternate cameras and 2D control, the PC control layer in place of the legs.
    void behave();
    // Duration of Harry's variable-speed clips (`status` the clip playing, at `keyframe`); 0 for the others.
    std::int32_t clip_duration(int status, int keyframe);

    // Packed clip status of Harry's table: entry `clip`, blend phase or playing.
    static constexpr int status(int clip, bool active) { return (clip << 1) | (active ? 1 : 0); }

private:
    static constexpr int kMapClipSlot = 76;  // the map's own clips follow the weapon's (56..75)
    static constexpr int kMapExtraClipSlot = 92;
    static constexpr int kMapExtraClipSource = 16;
    static constexpr int kMapExtraClipCount = 8;
    static constexpr std::int64_t kFlexNudge = 1;  // 0.1 degree

    // Clip numbers of Harry's data by name, looked up once.
    struct Clips {
        bool known = false;
        int still = 0, walk_forward = 0, run_forward = 0, walk_backward = 0, sidestep_left = 0, sidestep_right = 0,
            run_left = 0, run_right = 0, idle_exhausted = 0, recoil = 0, aim_settle = 0, aim_fire = 0;
    };
    const Clips& clips();
    void attach_held_item(bool moving);
    void diagonal_legs();
    void alt_control();
    void alt_steer_2d(unsigned keys, std::int32_t pad);
    void alt_steer_orbit(unsigned keys, std::int32_t pad);
    bool alt_jump_back();
    void alt_quick_turn(bool jump_back);
    void alt_legs_clip();
    void alt_aim_and_fire();
    void alt_lower_state();
    void alt_katana_lunge();
    void credit_distance(std::int32_t dx, std::int32_t dz, bool running);
    void free_aim_pose();
    void rear_look();

    PlayerRecord& r_;
    Clips clips_;
    // The legs' walk/run hand-over: how long it has been blending (it is cut after half a second).
    std::int32_t walk_run_blend_time_ = 0;
    // The alternate cameras' diagonal legs: blend weight of the turned stride, fade-out of the last blended pose,
    // the side being blended, and that last pose (bones from the hips down).
    std::int32_t diag_weight_ = 0;
    std::int32_t diag_fade_ = 0;
    int diag_side_ = 0;
    BoneMatrix diag_last_[18];
    // The rear look's head yaw (eases toward over-the-shoulder while held).
    std::int32_t rear_head_yaw_ = 0;

    // The PC control layer's own state between ticks.
    struct AltControl {
        // 2D control: the fixed camera last tick, the heading lock across a shot change, the last travel.
        std::int32_t cam_x = 0, cam_y = 0, cam_z = 0, cam_yaw = 0;
        bool have_cam = false;
        bool locked = false;
        std::int32_t lock_heading = 0, lock_stick = 0;
        std::int32_t travel_yaw = 0;
        bool have_travel = false;
        std::uint32_t last_run_ms = 0;
        int map = -1, room = -1;
        // The jump back: last tick's back press, the hop running, its time and the clip time last tick.
        bool prev_back = false;
        bool jumping = false;
        std::int32_t jump_elapsed = 0, jump_prev_time = -1;
        // The quick turn: running, its start yaw and the turn so far.
        bool turning = false;
        std::int32_t turn_start = 0, turn_done = 0;
        // The side step's clip time last tick.
        std::int32_t step_prev_time = -1;
        // Aim and fire: aim and fire last tick, the melee press time and the tap pulse's end.
        bool prev_aim = false, prev_fire = false;
        std::uint32_t fire_down_ms = 0, tap_until_ms = 0;
        // The katana's forward lunge speed.
        std::int32_t lunge = 0;
    } alt_;
};

// The game's player. The record is bound once by the game (the bridge) before the first call.
void bind_player_record(PlayerRecord* record);
Player& player();

}  // namespace sh::game
