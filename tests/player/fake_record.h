// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A PlayerRecord over plain fields for the tests of sh::game::Player: every value a slot, the actions logged.

#pragma once

#include "engine/player/player_record.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sh::test {

class FakePlayerRecord : public sh::game::PlayerRecord {
public:
    std::array<std::int64_t, static_cast<std::size_t>(sh::game::PlayerVar::Count)> vars{};
    std::vector<std::string> log;
    std::array<sh::game::InventorySlot, 41> slots{};
    int slot_count = 8;
    int equipped = 0;
    int cursor_item = -1;
    std::array<int, 256> starts{}, ends{}, links{};
    bool has_rig = true;
    int max_hp = 409600;

    std::int64_t get(sh::game::PlayerVar v) const override { return vars[static_cast<std::size_t>(v)]; }
    void set(sh::game::PlayerVar v, std::int64_t value) override { vars[static_cast<std::size_t>(v)] = value; }
    std::int64_t& at(sh::game::PlayerVar v) { return vars[static_cast<std::size_t>(v)]; }

    void reset_held_weapon_model() override { log.push_back("held_model_reset"); }
    void clear_damage() override { log.push_back("damage_clear"); }
    void read_from_save() override { log.push_back("read_save"); }
    void assign_harry_models() override { log.push_back("models"); }
    void reset_save_player() override { log.push_back("reset_save"); }
    int inventory_slot_count() const override { return slot_count; }
    void set_inventory_slot_count(int count) override { slot_count = count; }
    sh::game::InventorySlot inventory_slot(int idx) const override { return slots[static_cast<std::size_t>(idx)]; }
    int equipped_item() const override { return equipped; }
    void set_inventory_cursor_item(int item) override { cursor_item = item; }
    // Test numbering: items 128.. are melee (attack = item - 128), 160.. guns (attack = item - 128); ammo of a
    // gun item is item + 32 (mod 256).
    int ammo_item_of(int item) const override { return (item + 32) & 0xFF; }
    int weapon_attack_of_item(int item, bool* gun) const override {
        *gun = item >= 160 && item < 192;
        return item >= 128 && item < 192 ? item - 128 : -1;
    }
    int max_health() const override { return max_hp; }
    void install_map_clips(int dst, int src, int count) override {
        log.push_back("map_clips " + std::to_string(dst) + " " + std::to_string(src) + " " + std::to_string(count));
    }
    void play_clip(sh::game::PlaybackHalf half, int status) override {
        log.push_back(std::string(half == sh::game::PlaybackHalf::Body ? "play body " : "play upper ") +
                      std::to_string(status));
    }
    // Harry's clip numbers (gamedata/chara/Harry/character.json "anims") for the names the engine asks.
    int clip_index(const char* name) const override {
        static const std::pair<const char*, int> kClips[] = {
            {"still", 0}, {"walk_forward", 2}, {"run_forward", 3}, {"walk_backward", 4}, {"sidestep_left", 5},
            {"sidestep_right", 6}, {"run_left", 7}, {"run_right", 8}, {"idle_exhausted", 27},
            {"handgun_aim_start", 29}, {"weapon_attack_2", 30}, {"handgun_recoil", 31}};
        for (const auto& c : kClips) {
            if (std::string(name) == c.first) {
                return c.second;
            }
        }
        return -1;
    }
    int clip_start(int status) const override { return starts[static_cast<std::size_t>(status)]; }
    int clip_end(int status) const override { return ends[static_cast<std::size_t>(status)]; }
    int clip_link(int status) const override { return links[static_cast<std::size_t>(status)]; }
    bool install_weapon_rig(int attack) override {
        log.push_back("rig " + std::to_string(attack));
        return has_rig;
    }
    void clear_weapon_info() override { log.push_back("weapon_info_clear"); }
    void load_weapon_animation(int attack) override { log.push_back("weapon_anim " + std::to_string(attack)); }
    const char* weapon_name(int attack) const override {
        switch (attack) {
            case 0: return "KitchenKnife";
            case 32: return "Handgun";
            default: return nullptr;
        }
    }
    void sound_command(int command) override { log.push_back("sound " + std::to_string(command)); }
    void attach_held_item(int variant, int joint) override {
        log.push_back("held " + std::to_string(variant) + "," + std::to_string(joint));
    }

    void receive_damage() override { log.push_back("damage"); }
    void run_controller() override { log.push_back("controller"); }
    void run_legs() override { log.push_back("legs"); }
    void run_footsteps() override { log.push_back("footsteps"); }
    void run_upper_body() override { log.push_back("upper"); }
    void rest_before() override { log.push_back("rest_before"); }
    bool idle_taken = false, state_taken = false;
    bool rest_idle() override {
        log.push_back("rest_idle");
        return idle_taken;
    }
    bool rest_state() override {
        log.push_back("rest_state");
        return state_taken;
    }
    void rest_after() override { log.push_back("rest_after"); }
    void timer_update() override { log.push_back("timer"); }
    void map_player_tick() override { log.push_back("map_tick"); }
    sh::game::MoveResult slide(std::int32_t dx, std::int32_t dz) override { return {dx, 0, dz, {}}; }
    int speed_zone(std::int32_t, std::int32_t) const override { return 1; }
    int weapon_clip_start(int) const override { return 0; }
    unsigned keys = 0;
    unsigned movement_keys() const override { return keys; }
    std::int32_t cam_yaw = 0;
    void view_camera(std::int32_t position[3], std::int32_t* yaw) const override {
        position[0] = position[1] = position[2] = 0;
        *yaw = cam_yaw;
    }
    std::uint32_t now_ms = 0;
    std::uint32_t time_ms() const override { return now_ms; }
    void scripted_control() override { log.push_back("scripted_control"); }
    void scripted_move() override { log.push_back("scripted_move"); }
    void keyframe_inspector() override { log.push_back("inspector"); }

    sh::maps::MapRules rules;  // the map's overlay.json rules
    bool character_push = false;  // a character pushes Harry on a map without a pushing floor
    sh::game::GroundSurface ground;
    sh::game::MoveResult move;  // what the walls let through (dx/dy/dz < 0 x: pass the wish through)
    bool pass_through = true;
    std::array<sh::game::BoneMatrix, 18> bones{}, scratch{};
    int bone_count = 18, keyframe_count = 1000;
    std::array<int, 64> rows{};

    const sh::maps::MapRules& map_rules() const override { return rules; }
    sh::game::GroundSurface ground_at(std::int32_t, std::int32_t) override { return ground; }
    sh::game::MoveResult move_through_walls(std::int32_t dx, std::int32_t dy, std::int32_t dz, bool,
                                            const sh::game::GroundSurface&) override {
        if (pass_through) {
            sh::game::MoveResult r = move;
            r.dx = dx;
            r.dy = dy;
            r.dz = dz;
            return r;
        }
        return move;
    }
    void push_feedback(std::int32_t dx, std::int32_t dz) override {
        log.push_back("push " + std::to_string(dx) + "," + std::to_string(dz));
    }
    bool character_push_take() override { return character_push; }
    void count_phantom_floor() override { log.push_back("phantom"); }
    int reaction_row_count() const override { return 0; }
    sh::game::ReactionRow reaction_row(int row) const override { return {0, rows[static_cast<std::size_t>(row)], 0}; }
    bool reaction_row_imported(int) const override { return false; }
    void rotate_bone(int bone, int z, int x, int y, bool refresh) override {
        log.push_back("rot " + std::to_string(bone) + " " + std::to_string(z) + " " + std::to_string(x) + " " +
                      std::to_string(y) + (refresh ? " r" : ""));
    }
    sh::game::BoneMatrix bone_local(int bone) const override { return bones[static_cast<std::size_t>(bone)]; }
    void set_bone_local(int bone, const sh::game::BoneMatrix& m, bool) override {
        bones[static_cast<std::size_t>(bone)] = m;
    }
    void pose_keyframes(int kf0, int kf1, int) override {
        log.push_back("pose " + std::to_string(kf0) + " " + std::to_string(kf1));
    }
    void pose_scratch(int, int, int) override {}
    sh::game::BoneMatrix scratch_bone(int bone) const override { return scratch[static_cast<std::size_t>(bone)]; }
    int anim_bone_count() const override { return bone_count; }
    int anim_keyframe_count() const override { return keyframe_count; }
    bool bone_keys_translation(int) const override { return true; }
    bool bone_keys_rotation(int) const override { return true; }
    std::array<std::int32_t, 3> root{};
    void set_root_translation(std::int32_t x, std::int32_t y, std::int32_t z) override { root = {x, y, z}; }
};

}  // namespace sh::test
