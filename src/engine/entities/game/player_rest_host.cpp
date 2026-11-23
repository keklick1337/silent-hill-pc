// Harry's damage, grabs, reaction states, death and the tick's own timers are Lua (gamedata/chara/Harry/damage.lua);
// sh::game::Player calls the script's receive_damage, logic_pre, idle,
// state_update and logic_post. The script reaches the game through named calls:
//   sh.entity.get/set(0, "...")   the record fields below (and the base list);
//   sh.entity.harry / set_harry   the state outside the record (PlayerVar);
//   sh.entity.* (below)           the map's reaction rows, the engine's resets and the game over.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/config/gameplay_config.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/math/fixed_geometry.h"
#include "engine/player/player.h"
#include "engine/player/player_records.h"
#include "engine/runtime/game_hooks.h"
#include "engine/script/sh_script.h"
#include "engine/entities/game/grab_reaction_check.h"

// --- other modules (plain-type declarations) ---
extern "C" {
const sh::game::ClipTableEntry* HarryMap_ClipTable(void);
int HarryMap_ReactionRow(int row, int* status, int* trigger, int* time, int* last);
int HarryAttack_SoundClass(int attack);
int Pc_HarryReactions_Row(int id, int* first, int* last);
int Pc_HarryReactions_EnsureFor(int chara_id, int hostile);
void Player_AnimLock(void);
std::int32_t WorldEnv_LightIntensityGet(std::int16_t angles[4]);
}

namespace {

namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::Field;
using sh::chara::behavior::Kind;
using sh::chara::behavior::fail;
using sh::game::HarryVars;

constexpr int kHarry = 1;
constexpr int kNpcSlots = ent::kNpcSlotCount;

sh::game::Session& s() { return sh::game::session(); }

#define G(name, member, kind) \
    {name, offsetof(ent::CharacterRecord, member), SH_FIELD_KIND(ent::CharacterRecord, member, kind)}
#define H(name, member, kind) \
    {name, offsetof(ent::CharacterRecord, vars) + offsetof(HarryVars, member), SH_FIELD_KIND(HarryVars, member, kind)}

const Field kFields[] = {
    // The NPC that attacked Harry (its game index, -1 none).
    G("harry_attacker", partner, S32),
    G("damage_amount", damage.amount, S32),
    G("damage_x", damage.push.x, S32),
    G("damage_y", damage.push.y, S32),
    G("damage_z", damage.push.z, S32),
    // -1 until a hit reaction has begun (the attack code reads it).
    G("harry_attack_marker", attack.phase, S16),
    // 0 Harry fell forward, 1 backward.
    H("harry_fall_direction", fall_direction, U8),
    // The facing a push (the Split Head's) turns him to.
    H("harry_push_angle", push_angle, S16),
    // Runs while the stun flag is set; reset at its limit.
    H("harry_flag_timer", flag_timer, S32),
    G("harry_rot_speed_y", rotation_speed.y, S16),
};

#undef H
#undef G

const char* const kNames[] = {
    "entity.overlay_row",        "entity.map_clip_start",       "entity.overlay_anim_start",
    "entity.overlay_anim_set",
    "entity.harry_state_reset",  "entity.harry_collision_state_reset",
    "entity.harry_game_over",    "entity.harry_player_anim_lock",
    "entity.harry_damage_hook",
    "entity.grab_playable",      "entity.grab_fallback_playable", "entity.grab_escape_count",
    "entity.max_health",         "entity.max_saturation",       "entity.attack_sound_class",
    "entity.harry_pose_matrix_update", "entity.harry_light_angle", "entity.harry_anim_flags_set",
    "entity.harry_reaction_row",       "entity.harry_reactions_ensure", "entity.npc_slot_count",
};

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    ent::CharacterRecord& player = s().system.characters.player;
    if (fn == "overlay_row") {
        // overlay_row(i) -> status, status_2, time, last keyframe: row `i` of the map's table of Harry's
        // reaction clips (the clip's id is status_2; the table is the map's own).
        const long long i = a.integer(0);
        int row[4] = {};
        if (i < 0 || i > 255 || HarryMap_ReactionRow(static_cast<int>(i), &row[0], &row[1], &row[2], &row[3]) == 0) {
            out[0] = out[1] = out[2] = out[3] = 0;
        } else {
            out[0] = row[0], out[1] = row[1], out[2] = row[2], out[3] = row[3];
        }
        return 4;
    }
    if (fn == "map_clip_start") {
        // map_clip_start(i) -> first keyframe of the map's clip `i` for Harry.
        const long long i = a.integer(0);
        const sh::game::ClipTableEntry* clips = HarryMap_ClipTable();
        out[0] = (clips == nullptr || i < -512 || i > 512) ? 0 : clips[i].start_keyframe;
        return 1;
    }
    if (fn == "overlay_anim_start") {
        // overlay_anim_start(status_2): the map's row for the reaction becomes Harry's clip, once per state.
        sh::game::player().start_reaction_clip(static_cast<int>(a.integer(0)));
        return 0;
    }
    if (fn == "overlay_anim_set") {
        // overlay_anim_set(status_2): the same row without the once-per-state guard -- both models jump
        // to the clip's next status at its own start time (the mid-reaction switch of the push scenes).
        sh::game::player().switch_reaction_clip(static_cast<int>(a.integer(0)));
        return 0;
    }
    if (fn == "harry_state_reset") {
        sh::game::player().reset_state();
        return 0;
    }
    if (fn == "harry_collision_state_reset") {
        // harry_collision_state_reset(restore): for the map scripts, Harry's own collision_state_reset
        // (gamedata/chara/Harry/state.lua) -- the code lives with Harry, the map only asks for it.
        long long ignored = 0;
        if (ShScript_CharaBehaviorHas(kHarry, "Harry") == 0 ||
            ShScript_CharaBehaviorCall(kHarry, "collision_state_reset", a.integer(0) != 0 ? 1 : 0, 0, &ignored) == 0) {
            return fail(err, errCap, "chara/Harry/state.lua: collision_state_reset failed");
        }
        return 0;
    }
    if (fn == "harry_game_over") {
        s().set_sys_state_next(sh::game::sys_state::kGameOver);
        sh::hooks::game_player_death();
        return 0;
    }
    if (fn == "harry_damage_hook") {
        // harry_damage_hook(): mods see the hit about to be deducted (player.damage) and may change or cancel it;
        // the record's damage amount becomes what they decided. Without a handler nothing is touched.
        const int amount = sh::hooks::game_player_damage(player.damage.amount, player.partner);
        if (amount != player.damage.amount) {
            player.damage.amount = amount;
        }
        return 0;
    }
    if (fn == "harry_player_anim_lock") {
        Player_AnimLock();
        return 0;
    }
    if (fn == "grab_playable") {
        out[0] = sh::entities::grab_reaction_playable(static_cast<int>(a.integer(0)), a.integer(1) != 0) ? 1 : 0;
        return 1;
    }
    if (fn == "grab_fallback_playable") {
        out[0] = sh::entities::hit_reaction_playable(a.integer(0) != 0) ? 1 : 0;
        return 1;
    }
    if (fn == "grab_escape_count") {
        // grab_escape_count(kind) -> the presses that free Harry from the grab (0 floor, 1/2 pinned front/back,
        // 3/4 torso, 5/6 legs, 7/8 neck), by the difficulty (gamedata/config/gameplay.json).
        out[0] = Gameplay_GrabEscapeCount(static_cast<int>(a.integer(0)), sh::game::save_record().difficulty());
        return 1;
    }
    if (fn == "max_health") {
        out[0] = Gameplay_MaxHealthQ12();
        return 1;
    }
    if (fn == "max_saturation") {
        out[0] = Gameplay_MaxSaturationQ12();
        return 1;
    }
    if (fn == "attack_sound_class") {
        // attack_sound_class(attack) -> the class of the attack that picks Harry's damage sound (0, 2, 4, 5).
        out[0] = HarryAttack_SoundClass(static_cast<int>(a.integer(0, -1)));
        return 1;
    }
    if (fn == "harry_pose_matrix_update") {
        ent::BoneCoord& root = s().system.characters.player_bones[0];
        root.world_current = 0;
        const sh::math::Matrix3 rot = sh::math::rot_matrix_zxy_neg({player.rotation.x, player.rotation.y, player.rotation.z});
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) root.local.m[r][c] = rot.m[r][c];
        return 0;
    }
    if (fn == "harry_light_angle") {
        // The world light's direction as the idle state keeps it (the original copies its y into the
        // quick-turn heading).
        std::int16_t light[4] = {};
        WorldEnv_LightIntensityGet(light);
        out[0] = light[1];
        return 1;
    }
    if (fn == "npc_slot_count") {
        // npc_slot_count() -> how many NPC slots the game has (an NPC's game index is 0..count-1).
        out[0] = kNpcSlots;
        return 1;
    }
    if (fn == "harry_reaction_row") {
        // harry_reaction_row(id) -> row index, first keyframe, last keyframe of Harry's reaction `id` on this
        // map (anim << 1 | active); -1, 0, 0 when the map has none.
        int first = 0;
        int last = 0;
        out[0] = Pc_HarryReactions_Row(static_cast<int>(a.integer(0, -1)), &first, &last);
        out[1] = first, out[2] = last;
        return 3;
    }
    if (fn == "harry_reactions_ensure") {
        // harry_reactions_ensure(chara_id[, hostile]) -> how many of the reactions the type's attacks need stay
        // missing on this map after loading them from its own maps (-1: the data is unreadable).
        out[0] = Pc_HarryReactions_EnsureFor(static_cast<int>(a.integer(0, 0)), static_cast<int>(a.integer(1, 0)));
        return 1;
    }
    if (fn == "harry_anim_flags_set") {
        // Both halves of Harry's model get the animation flags.
        const auto flags = static_cast<std::uint16_t>(a.integer(0));
        s().system.characters.player_upper.model.anim.flags |= flags;
        player.model.anim.flags |= flags;
        return 0;
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    kFields, std::size(kFields), kNames, std::size(kNames), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
