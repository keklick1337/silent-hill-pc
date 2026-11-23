// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The replay harness's probe (engine/devtools/replay.h): what the harness hashes and dumps
// each tick, read from the engine's state: the frame counters (engine/loop/frame_view.h), the character slots of
// the session (CharacterRecord) and the player's upper body record. The dump format is the harness's; the values
// keep the widths and signs of the records they come from.

#include "engine/devtools/replay.h"
#include "engine/game_state/session.h"

#include "engine/debug/console_host.h"
#include "engine/loop/frame_view.h"
#include "engine/timing/vblank.h"

namespace {

namespace ent = sh::entities;
namespace dt = sh::devtools;

ent::CharacterSlots& slots() { return sh::game::session().system.characters; }

void fill(dt::ReplayCharacter& out, const ent::CharacterRecord& c) {
    out.chara_id = c.model.chara_id;
    out.control_state = c.model.control_state;
    out.state_step = c.model.control_step;
    out.anim_status = c.model.anim.status;
    out.anim_flags = c.model.anim.flags;
    out.keyframe = c.model.anim.keyframe;
    out.anim_time = c.model.anim.time;
    out.anim_alpha = c.model.anim.alpha;
    out.position[0] = c.position.x;
    out.position[1] = c.position.y;
    out.position[2] = c.position.z;
    out.rotation[0] = c.rotation.x;
    out.rotation[1] = c.rotation.y;
    out.rotation[2] = c.rotation.z;
    out.move_speed = c.move_speed;
    out.fall_speed = c.fall_speed;
    out.heading = c.heading;
    out.flags = c.flags;
    out.npc_index = c.partner;
    out.attack_received = c.attack_received;
    out.health = c.health;
    out.death_timer = c.death_timer;
    out.timer = c.timer;
}

// The field-by-field digests: every value of a record in declaration order, sign- or zero-extended to 32 bits.
class FieldHash {
public:
    template <class T>
    FieldHash& operator()(T v) {
        h_ = dt::replay::fnv_s32(h_, static_cast<std::int32_t>(v));
        return *this;
    }
    template <class T, std::size_t N>
    FieldHash& operator()(const T (&values)[N]) {
        for (const T& v : values) {
            (*this)(v);
        }
        return *this;
    }
    std::uint32_t value() const { return h_; }

private:
    std::uint32_t h_ = dt::replay::kFnvBasis;
};

void hash_save(FieldHash& h, const sh::game::SaveRecord& r) {
    for (const sh::game::SaveItem& item : r.items) {
        h(item.item)(item.count)(item.command)(item.extra);
    }
    h(r.player_reset)(r.map)(r.room)(r.save_count)(r.location)(r.paper_map)(r.equipped_weapon)(r.slot_count);
    h(r.item_toggles)(r.enemy_states)(r.paper_maps)(r.event_flags)(r.health_saturation)(r.picked_up_items);
    h(r.inventory_flags)(r.health)(r.position_x)(r.rotation_y)(r.clear_count)(r.endings)(r.position_z)(r.play_time);
    h(r.run_distance)(r.walk_distance)(r.mode_bits)(r.melee_kills)(r.melee_kills_b)(r.ranged_kills);
    h(r.hits_and_difficulty)(r.counters)(r.current_ending)(r.continues);
}

void hash_settings(FieldHash& h, const sh::game::Settings& s) {
    const sh::game::PadBinds& b = s.binds;
    h(b.enter)(b.cancel)(b.skip)(b.action)(b.aim)(b.light)(b.run)(b.view)(b.step_left)(b.step_right)(b.pause);
    h(b.item)(b.map)(b.option);
    h(s.screen_x)(s.screen_y)(s.mono)(s.music_volume)(s.sfx_volume)(s.vibration)(s.brightness);
    h(s.weapon_control_press)(s.blood_color)(s.auto_load)(s.extra_options)(s.view_reverse)(s.self_view);
    h(s.retreat_turn_reverse)(s.walk_run_reverse)(s.auto_aim_off)(s.bullet_multiplier)(s.seen_game_over_tips);
    h(s.pal_language);
}

void hash_raw_pad(FieldHash& h, const sh::game::RawPad& p) {
    h(p.status)(p.kind)(p.buttons)(p.right_x)(p.right_y)(p.left_x)(p.left_y);
}

void hash_pad(FieldHash& h, const sh::game::PadState& p) {
    hash_raw_pad(h, p.raw);
    h(p.pulse_ticks)(p.held)(p.clicked)(p.released)(p.pulsed)(p.pulsed_menu);
    for (const sh::game::PadSticks* s : {&p.sticks, &p.sticks_past_dead_zone, &p.steer}) {
        h(s->right_x)(s->right_y)(s->left_x)(s->left_y);
    }
}

// The game block without the raw pad (the host writes it from live input, so two runs of one replay differ there).
std::uint32_t game_digest(const sh::game::GameBlock& g) {
    FieldHash h;
    hash_settings(h, g.settings);
    hash_pad(h, g.pads[0]);
    hash_pad(h, g.pads[1]);
    hash_save(h, g.autosave);
    hash_save(h, g.save);
    h(g.screen_width)(g.screen_height)(g.clear_color.r)(g.clear_color.g)(g.clear_color.b)(g.clear_color.x);
    h(g.game_state_previous)(g.game_state)(g.game_steps)(g.demo_marks)(g.map_anim_index)(g.music_track);
    h(g.ambient_bank);
    return h.value();
}

std::uint32_t combat_digest(const sh::game::PlayerCombat& c) {
    FieldHash h;
    h(c.attack_position.x)(c.attack_position.y)(c.attack_position.z)(c.weapon_attack)(c.ammo_loaded)(c.ammo_total);
    h(c.weapon_slot)(c.aiming);
    return h.value();
}

// The player's own variables: his whole variable store.
constexpr std::size_t kPlayerPropertyBytes = ent::kCharacterVarBytes;

class EngineReplayProbe final : public dt::ReplayProbe {
public:
    dt::ReplayDigests digests() override {
        const sh::game::Session& s = sh::game::session();
        FieldHash save;
        hash_save(save, s.game.save);
        return dt::ReplayDigests{save.value(), game_digest(s.game), combat_digest(s.system.combat)};
    }
    dt::ReplayClock clock() override {
        dt::ReplayClock c;
        c.tick = sh::loop::frame_ticks();
        c.vblanks = sh::loop::frame_vblanks();
        c.uncapped_vblanks = sh::loop::frame_uncapped_vblanks();
        c.interval_vblanks = sh::loop::frame_interval_vblanks();
        c.delta_time = sh::loop::frame_delta();
        c.delta_time_raw = sh::loop::frame_delta_raw();
        c.gravity = sh::loop::frame_gravity();
        c.demo_frames = sh::loop::demo_vblanks();
        c.warm_boot_timer = sh::loop::reset_hold_vblanks();
        return c;
    }
    void character(int slot, dt::ReplayCharacter& out) override { fill(out, *slots().record(slot)); }
    void player_extra(dt::ReplayPlayerExtra& out) override {
        const ent::UpperBodyRecord& u = slots().player_upper;
        out.anim_status = u.model.anim.status;
        out.keyframe = u.model.anim.keyframe;
        out.anim_time = u.model.anim.time;
        out.anim_alpha = u.model.anim.alpha;
        out.state = u.state;
        out.upper_body = u.upper_state;
        out.lower_body = u.lower_state;
        out.last_used_item = u.last_used_item;
    }
    std::span<const std::uint8_t> player_properties() override {
        return {slots().player.vars.bytes, kPlayerPropertyBytes};
    }
    bool virtual_time() override { return sh::timing::virtual_time::active(); }
    bool virtual_time_paced() override { return sh::timing::virtual_time::paced(); }
    void console(const std::string& line) override { Pc_ConsoleExec(line.c_str()); }
};

EngineReplayProbe s_probe;

// Installed during static initialisation: the harness reads it from its first call on.
[[maybe_unused]] const bool s_probe_set = (dt::set_replay_probe(&s_probe), true);

}  // namespace
