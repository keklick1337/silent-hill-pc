// Harry under a scene's control: the map
// header's player entries, defined once here; their logic is Harry's Lua (gamedata/chara/Harry/scripted.lua,
// cutscene.lua with the per-map list map_states.lua). Without the script every entry does nothing (and answers 0);
// the reason is logged once per entry. The script reaches the game through the named calls below.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/session.h"
#include "engine/math/geometry_state.h"
#include "engine/player/player_records.h"
#include "engine/script/sh_script.h"

// --- other modules (plain-type declarations) ---
extern "C" {
extern sh::game::ClipTableEntry HARRY_BASE_ANIM_INFOS[256];
void Anim_PlaybackOnce(void* model, void* anim_file, void* bones, sh::game::ClipTableEntry* entry);
void Anim_BlendLinear(void* model, void* anim_file, void* bones, sh::game::ClipTableEntry* entry);
std::int32_t Anim_DurationGet(void* model, sh::game::ClipTableEntry* entry);
void Collision_SurfaceGet(sh::game::GroundRecord* ground, std::int32_t x, std::int32_t z);
std::int32_t Collision_WallDetect(sh::game::WallResult* result, const std::int32_t* offset, void* chara);
std::int32_t WorldGfx_PlayerPrevHeldItem(sh::game::PlayerCombat* combat);
void WorldGfx_HeldItemShow(void);
void WorldGfx_HeldItemHide(void);
void WorldGfx_HeldItemAttach(int chara_id, std::int32_t model_bone);
void Vibration_DamageRumble(std::int32_t damage);
void Combat_WeaponEffectSpawn(std::int32_t weapon_attack, std::int16_t rot_y, std::int16_t rot_x,
                              sh::entities::BoneCoord* coord);
// The map's own words: its clip table, the weapon stash and the wall blocks.
const sh::game::ClipTableEntry* HarryMap_ClipTable(void);
std::uint8_t* HarryMap_WeaponStashWord(void);
sh::game::WallResult* HarryMap_WallBlock(int own);
std::int32_t* HarryMap_PushTotalWord(void);
}

namespace {

namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::Field;
using sh::chara::behavior::Kind;

constexpr int kHarry = 1;
constexpr long long kMapClipFirst = 76;  // packed status of Harry's first map clip (clip 38, blending)

sh::game::Session& s() { return sh::game::session(); }
ent::BoneCoord* bones() { return s().system.characters.player_bones; }

// Record fields of any character used by Harry/scripted.lua (the padlock of map2_s00).
const Field kFields[] = {
    // Index of the NPC the character is attached to (the player: its attacker); -1 none.
    {"attacker_index", offsetof(ent::CharacterRecord, partner), SH_FIELD_KIND(ent::CharacterRecord, partner, S32)},
    // The collision box's outer extent (the cylinder radius is set from it).
    {"box_outer_extent", offsetof(ent::CharacterRecord, collision.box.extent_x),
     SH_FIELD_KIND(ent::CharacterRecord, collision.box.extent_x, S16)},
};

// The root of Harry's skeleton while a scene moves him.
ent::BoneCoord* s_coords = nullptr;
// The pin of a grabbing monster while pin_update runs: where it holds Harry (in-out) and its facing.
std::int32_t* s_pin_x = nullptr;
std::int32_t* s_pin_z = nullptr;
const std::int16_t* s_pin_angle = nullptr;
// The push of the Split Head while push_update runs (x, y, z).
const std::int32_t* s_push = nullptr;
// The record slot of the character pushing while push_update runs: 0 for the boss map's own push, else an
// engine-spawned Split Head off its maps.
int s_pusher = 0;
// The shove gathered on a map without its own word (an engine-spawned Split Head's push).
std::int32_t s_foreign_push_total = 0;
// The points (x, y, z each) and the end heading of the scripted walk while path_walk runs.
const std::int32_t* s_path = nullptr;
std::int16_t s_path_angle = 0;
// The turn of the last cutscene tick (Q12 angle * 16 per second): the move reads it, freezing clears it.
std::int32_t s_turn = 0;

const char* const kNames[] = {
    "entity.harry_ground_tilt",        "entity.harry_wall_detect",        "entity.harry_wall_floor_set",
    "entity.harry_cutscene_root_set",  "entity.harry_held_item_previous", "entity.harry_weapon_stash",
    "entity.harry_set_weapon_stash",   "entity.harry_path_point",         "entity.harry_path_angle",
    "entity.harry_pin_get",            "entity.harry_pin_set",            "entity.harry_push_vector",
    "entity.harry_push_total",         "entity.harry_set_push_total",     "entity.harry_pusher",     "entity.harry_damage_rumble",
    "entity.harry_clip_playback",      "entity.harry_clip_duration",      "entity.harry_held_item_show",
    "entity.harry_held_item_hide",     "entity.harry_held_item_attach",   "entity.harry_anim_playback_state",
    "entity.harry_cutscene_pose_update", "entity.harry_cutscene_weapon_effect",
};

// Harry's clip `status`: statuses below the map's clips are the base table's (the original read before the map's
// table there).
sh::game::ClipTableEntry* clip_entry(long long status) {
    if (status >= 0 && status < kMapClipFirst) {
        return &HARRY_BASE_ANIM_INFOS[status];
    }
    const sh::game::ClipTableEntry* map = HarryMap_ClipTable();
    if (status >= 0 && status <= 255 && map != nullptr) {
        return const_cast<sh::game::ClipTableEntry*>(&map[status - kMapClipFirst]);
    }
    return nullptr;
}

long long harry_value(const char* fn);

std::int32_t q12_to_q8(std::int32_t v) { return v >> 4; }

// A model bone packs the mesh variant in the low nibble and the joint above it (16 bits).
std::int32_t model_bone(int variant, int joint) { return static_cast<std::int16_t>(variant | (joint << 4)); }

int call(std::string_view fn, const Args& a, long long* out, char*, int) {
    if (fn == "harry_ground_tilt") {
        // harry_ground_tilt(x, z) -> tilt x, tilt z (Q12 angles) of the ground there.
        sh::game::GroundRecord g{};
        Collision_SurfaceGet(&g, static_cast<std::int32_t>(a.integer(0)), static_cast<std::int32_t>(a.integer(1)));
        out[0] = g.tilt_x, out[1] = g.tilt_z;
        return 2;
    }
    if (fn == "harry_wall_detect") {
        // harry_wall_detect(dx, dy, dz, own) -> the move cut by the walls (x, y, z), the floor height and
        // type; `own` 1: the result goes to the map's own block (when the map has one), else the shared one.
        sh::game::WallResult* block = HarryMap_WallBlock(a.integer(3) != 0 ? 1 : 0);
        const std::int32_t offset[3] = {static_cast<std::int32_t>(a.integer(0)), static_cast<std::int32_t>(a.integer(1)),
                                        static_cast<std::int32_t>(a.integer(2))};
        Collision_WallDetect(block, offset, &s().system.characters.player);
        out[0] = block->offset[0], out[1] = block->offset[1], out[2] = block->offset[2];
        out[3] = block->ground_height, out[4] = block->ground_type;
        return 5;
    }
    if (fn == "harry_wall_floor_set") {
        // harry_wall_floor_set(own, height): the floor height kept in that result block.
        HarryMap_WallBlock(a.integer(0) != 0 ? 1 : 0)->ground_height = static_cast<std::int32_t>(a.integer(1));
        return 0;
    }
    if (fn == "harry_cutscene_root_set") {
        // harry_cutscene_root_set(x, y, z): Harry's cutscene root at a world point (Q12, stored as Q8).
        if (s_coords != nullptr) {
            for (int i = 0; i < 3; ++i) {
                s_coords->local.t[i] = q12_to_q8(static_cast<std::int32_t>(a.integer(i)));
            }
        }
        return 0;
    }
    if (fn == "harry_held_item_previous") {
        // The held item model becomes the one of the current weapon attack (the previous held item).
        WorldGfx_PlayerPrevHeldItem(&s().system.combat);
        return 0;
    }
    if (fn == "harry_weapon_stash" || fn == "harry_set_weapon_stash") {
        // The weapon attack put aside while a scene empties Harry's hand (one byte of the map).
        std::uint8_t* word = HarryMap_WeaponStashWord();
        if (fn == "harry_weapon_stash") {
            out[0] = static_cast<std::int8_t>(*word);
            return 1;
        }
        *word = static_cast<std::uint8_t>(a.integer(0));
        return 0;
    }
    if (fn == "harry_path_point") {
        // harry_path_point(i) -> x, y, z of the walk's point `i`. The original reads one point past the
        // last when the walk ends on it; that read is kept (the event's point array has room).
        const long long i = a.integer(0);
        if (s_path == nullptr || i < 0 || i > 255) {
            out[0] = out[1] = out[2] = 0;
        } else {
            out[0] = s_path[i * 3], out[1] = s_path[i * 3 + 1], out[2] = s_path[i * 3 + 2];
        }
        return 3;
    }
    if (fn == "harry_pin_get") {
        // harry_pin_get() -> x, z, facing of the grabbing monster's pin (pin_update).
        out[0] = s_pin_x != nullptr ? *s_pin_x : 0;
        out[1] = s_pin_z != nullptr ? *s_pin_z : 0;
        out[2] = s_pin_angle != nullptr ? *s_pin_angle : 0;
        return 3;
    }
    if (fn == "harry_pin_set") {
        // harry_pin_set(x, z): the pin moves there (the monster follows).
        if (s_pin_x != nullptr && s_pin_z != nullptr) {
            *s_pin_x = static_cast<std::int32_t>(a.integer(0));
            *s_pin_z = static_cast<std::int32_t>(a.integer(1));
        }
        return 0;
    }
    if (fn == "harry_push_vector") {
        // harry_push_vector() -> x, z of this tick's shove (push_update).
        out[0] = s_push != nullptr ? s_push[0] : 0;
        out[1] = s_push != nullptr ? s_push[2] : 0;
        return 2;
    }
    if (fn == "harry_pusher") {
        // harry_pusher() -> the slot pushing while push_update runs; 0 for the boss map's own push.
        out[0] = s_pusher;
        return 1;
    }
    if (fn == "harry_push_total" || fn == "harry_set_push_total") {
        // The shove gathered so far (the map's own word; an engine spawn's push off the boss maps keeps its own).
        std::int32_t* word = HarryMap_PushTotalWord();
        if (word == nullptr && s_pusher != 0) {
            word = &s_foreign_push_total;
        }
        if (fn == "harry_push_total") {
            out[0] = word != nullptr ? *word : 0;
            return 1;
        }
        if (word != nullptr) {
            *word = static_cast<std::int32_t>(a.integer(0));
        }
        return 0;
    }
    if (fn == "harry_damage_rumble") {
        // harry_damage_rumble(amount): the pad rumbles for damage of that size.
        Vibration_DamageRumble(static_cast<std::int32_t>(a.integer(0)));
        return 0;
    }
    if (fn == "harry_clip_playback" || fn == "harry_clip_duration") {
        // harry_clip_playback(status) -> how Harry's clip `status` plays (1 once, 2 blend, 0 other), its first
        // and last keyframes; harry_clip_duration(status) -> its duration (Q12, negative: it plays backwards).
        sh::game::ClipTableEntry* info = clip_entry(a.integer(0));
        if (fn == "harry_clip_duration") {
            out[0] = info != nullptr ? Anim_DurationGet(&s().system.characters.player.model, info) : 0;
            return 1;
        }
        if (info == nullptr) {
            out[0] = out[1] = out[2] = 0;
            return 3;
        }
        out[0] = info->playback == Anim_PlaybackOnce ? 1 : info->playback == Anim_BlendLinear ? 2 : 0;
        out[1] = info->start_keyframe;
        out[2] = info->end_keyframe;
        return 3;
    }
    if (fn == "harry_path_angle") {
        out[0] = s_path_angle;
        return 1;
    }
    if (fn == "harry_held_item_show") {
        WorldGfx_HeldItemShow();
        return 0;
    }
    if (fn == "harry_held_item_hide") {
        WorldGfx_HeldItemHide();
        return 0;
    }
    if (fn == "harry_held_item_attach") {
        // harry_held_item_attach(part, index): the held item goes to Harry's model part (mesh variant, index).
        WorldGfx_HeldItemAttach(kHarry, model_bone(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1))));
        return 0;
    }
    if (fn == "harry_anim_playback_state") {
        // How the map's clip of Harry plays (1: it plays once, the match scene waits for it).
        out[0] = static_cast<std::int32_t>(harry_value("anim_playback_state"));
        return 1;
    }
    if (fn == "harry_cutscene_weapon_effect") {
        // harry_cutscene_weapon_effect(attack, rot_y, rot_x): a hit effect at Harry's own cutscene root;
        // entity.weapon_effect only reaches the shared NPC bones, the wrong pivot for Harry's bar-fight shots.
        if (s_coords != nullptr) {
            Combat_WeaponEffectSpawn(static_cast<std::int32_t>(a.integer(0)), static_cast<std::int16_t>(a.integer(1)),
                                     static_cast<std::int16_t>(a.integer(2)), s_coords);
        }
        return 0;
    }
    if (fn == "harry_cutscene_pose_update") {
        // Harry's root matrix from his rotation (the skeleton update of the map reads it).
        if (s_coords != nullptr) {
            s_coords->world_current = 0;
            const sh::entities::Angles& r = s().system.characters.player.rotation;
            sh::math::matrix_of(s_coords->local.m) = sh::math::rot_matrix_zxy_neg({r.x, r.y, r.z});
        }
        return 0;
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    kFields, std::size(kFields), kNames, std::size(kNames), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

// One function of Harry's scripted.lua; false (logged once per function) when it could not run.
bool harry(const char* fn, int a = 0, int b = 0, long long* out = nullptr) {
    long long result = 0;
    if (ShScript_CharaBehaviorHas(kHarry, "Harry") != 0 && ShScript_CharaBehaviorCall(kHarry, fn, a, b, &result) != 0) {
        if (out != nullptr) {
            *out = result;
        }
        return true;
    }
    static std::string reported;
    const std::string key = std::string(" ") + fn + " ";
    if (reported.find(key) == std::string::npos) {
        reported += key;
        std::fprintf(stderr, "[harry-overlay] chara/Harry/scripted.lua: %s did not run\n", fn);
    }
    if (out != nullptr) {
        *out = 0;
    }
    return false;
}

long long harry_value(const char* fn) {
    long long out = 0;
    harry(fn, 0, 0, &out);
    return out;
}

// The cutscene state of the tick (cutscene.lua's cutscene_update); false when the map's states are not the
// script's (Harry stands still, the reason is logged once).
bool cutscene_update(std::int32_t* turn) {
    static std::string checked_map;
    static bool handles = false;
    static bool reported_missing = false;
    static bool reported_error = false;
    if (ShScript_CharaBehaviorHas(kHarry, "Harry") == 0) {
        if (!reported_missing) {
            reported_missing = true;
            std::fprintf(stderr, "[harry-cutscene] chara/Harry/behavior.lua is missing: Harry's cutscene states do not run\n");
        }
        return false;
    }
    const char* map = ShScript_MapName();
    if (map == nullptr || checked_map != map) {
        checked_map = map != nullptr ? map : "";
        long long yes = 0;
        handles = ShScript_CharaBehaviorCall(kHarry, "cutscene_handles", 0, 0, &yes) != 0 && yes != 0;
        if (!handles) {
            std::fprintf(stderr, "[harry-cutscene] %s: chara/Harry/cutscene.lua does not handle every state of the map; "
                                 "Harry's cutscene states do not run on this map\n", checked_map.c_str());
        }
    }
    if (!handles) {
        return false;
    }
    s_coords = bones();
    long long result = 0;
    if (ShScript_CharaBehaviorCall(kHarry, "cutscene_update", 0, 0, &result) == 0) {
        // The tick may have half run: it is not run again.
        if (!reported_error) {
            reported_error = true;
            std::fprintf(stderr, "[harry-cutscene] chara/Harry/cutscene.lua: cutscene_update failed\n");
        }
        result = 0;
    }
    s_coords = nullptr;
    *turn = static_cast<std::int32_t>(result);
    return true;
}

}  // namespace

extern "C" {

void Player_SceneControl(void) {
    std::int32_t turn = 0;
    s_turn = cutscene_update(&turn) ? turn : 0;
}

void Player_SceneMove(void) {
    s_coords = bones();
    harry("cutscene_move", s_turn);
    s_coords = nullptr;
}

bool Player_PathWaypointExecute(std::int32_t state, const std::int32_t* points, std::int16_t angle, std::int32_t count) {
    s_path = points;
    s_path_angle = angle;
    long long done = 0;
    harry("path_walk", state, count, &done);
    s_path = nullptr;
    return done != 0;
}

std::int32_t Player_AnimPlaybackStateGet(void) { return static_cast<std::int32_t>(harry_value("anim_playback_state")); }

void Player_PushUpdate(std::int32_t x, std::int32_t z, const std::int32_t* push, int pusher) {
    s_push = push;
    s_pusher = pusher;
    harry("push_update", x, z);
    s_push = nullptr;
    s_pusher = 0;
}

// A per-tick map hook of Harry's behaviour.lua (overlay.json "player_updates": map2_s00's "padlock_update").
bool Player_MapUpdate(const char* fn) { return harry_value(fn) != 0; }

void Player_PinUpdate(std::int32_t* x, std::int32_t* z, const std::int16_t* angle) {
    s_pin_x = x, s_pin_z = z, s_pin_angle = angle;
    harry("pin_update");
    s_pin_x = s_pin_z = nullptr;
    s_pin_angle = nullptr;
}

void Player_CutsceneWeaponUnequip(void) {
    s_turn = 0;
    harry("cutscene_weapon_unequip");
}

void Player_ControlFreeze(void) {
    s_turn = 0;
    harry("control_freeze");
}

void Player_ControlUnfreeze(bool set_idle) { harry("control_unfreeze", set_idle ? 1 : 0); }
void Player_AnimStateSet(std::int32_t state) { harry("anim_state_set", state); }
void Player_AnimReset(void) { harry("anim_reset"); }
void Player_AnimLock(void) { harry("anim_lock"); }
void Player_AnimUnlock(void) { harry("anim_unlock"); }
bool Player_MoveSpeedIsZero(void) { return harry_value("move_speed_is_zero") != 0; }
void Player_MoveSpeedClear(void) { harry("move_speed_clear"); }
// The scene's weapon model is set up and hidden (only a map that lists it in Harry/scripted.lua does anything).
void Player_SceneWeaponHide(void) { harry("scene_weapon_hide"); }
void Player_FallBackward(void) { harry("fall_backward"); }
void Player_DamageFeetFront(void) { harry("damage_feet_front"); }
void Player_EmptyWeaponHandSet(void) { harry("weapon_stash"); }
void Player_WeaponAttackRestore(void) { harry("weapon_restore"); }

}  // extern "C"
